#include "network.h"
#include "config.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include "settings.h"

// Определение (выделение памяти) для RTC-переменных
RTC_DATA_ATTR PairingStatus pairingStatus = NOT_PAIRED;
RTC_DATA_ATTR uint8_t hostMac[6] = {0};
RTC_DATA_ATTR unsigned int globalReadingId = 0;
RTC_DATA_ATTR float lastSentMoisture = -100.0f;

// Системные переменные сети
volatile bool txDone = false;
bool updateMAC = false;

static uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static struct_pairing pairingData;
static struct_message myData;

extern bool calibration;
extern unsigned long lastCalibSignalTime;
extern void stopSoilMoisture();

// Внутренняя функция загрузки MAC (скрыта от main.cpp через static)
static void loadMacAddress() {
    uint8_t macBuf[6];
    Preferences preferences;

    preferences.begin("wifi_cfg", false);
    size_t macLength = preferences.getBytes("stored_mac", macBuf, 6);

    if (macLength == 6) {
        DEBUG_PRINTLN("[NVS] Считали ранее сохраненный кастомный MAC.");
    } else {
        DEBUG_PRINTLN("[NVS] Кастомный MAC не найден. Генерируем новый...");
        randomSeed(analogRead(0) + esp_random());
        for (int i = 0; i < 6; i++) {
            macBuf[i] = random(0, 256);
        }
        macBuf[0] = (macBuf[0] & 0xFE) | 0x02; // Локальный unicast
        preferences.putBytes("stored_mac", macBuf, 6);
    }
    preferences.end();

    if (esp_wifi_set_mac(WIFI_IF_STA, macBuf) == ESP_OK) {
        DEBUG_PRINT("[WIFI] Текущий рабочий MAC-адрес: ");
        DEBUG_PRINTLN(WiFi.macAddress());
    }
}

void sendSettingsReport() {
    struct_settings txSettings;
    txSettings.msgType = SETTINGS_REPORT;
    WiFi.macAddress(txSettings.macAddr);
    txSettings.airValue = r_airValue;
    txSettings.waterValue = r_waterValue;
    txSettings.timeToSleepSec = r_timeToSleepSec;
    txSettings.wifiTxPower = r_wifiTxPower;
    txSettings.moistureThreshold = r_moistureThreshold;
    txSettings.maxSkippedBoots = r_maxSkippedBoots;

    esp_now_send(hostMac, (uint8_t *)&txSettings, sizeof(txSettings));
    Serial.println("[NET] Текущие настройки отправлены на Хост.");
}

// Коллбэк отправки данных
static void OnDataSent(const wifi_tx_info_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("Status доставки пакета: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Успешно" : "Ошибка");
    if(status == ESP_NOW_SEND_SUCCESS) {
        lastSentMoisture = myData.hum;
    }
    txDone = true; 
}

// Коллбэк приема данных (сопряжение)
static void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
    if (len == 0) return;
    uint8_t type = incomingData[0];

    if (type == PAIRING) {
        if (len < sizeof(pairingData)) return; // ЗАЩИТА
        memcpy(&pairingData, incomingData, sizeof(pairingData));
        if (pairingData.isReply != 1) return;

        DEBUG_PRINTLN("Сопряжение успешно! MAC-адрес Хоста: ");
        for (int i = 0; i < 6; i++) {
            hostMac[i] = pairingData.macAddr[i];
            DEBUG_PRINTF("%02X:", hostMac[i]);
        }
        DEBUG_PRINTLN();

        esp_now_peer_info_t peerInfo;
        memset(&peerInfo, 0, sizeof(peerInfo));
        memcpy(peerInfo.peer_addr, hostMac, 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;

        if (esp_now_is_peer_exist(hostMac)) {
            esp_now_del_peer(hostMac);
        }

        if (esp_now_add_peer(&peerInfo) == ESP_OK) {
            Serial.println("Хост успешно добавлен в список пиров.");
            pairingStatus = PAIRED;
            sendSettingsReport(); 
        }
    } 
    else if (type == CALIB) {
        if (len < sizeof(struct_calib)) return; // ЗАЩИТА
        
        struct_calib rxCalib;
        memcpy(&rxCalib, incomingData, sizeof(rxCalib));

        // УБРАЛИ тупиковую проверку "if (!calibration)" и валидацию вольт
        if (rxCalib.mode == START) 
        {
            calibration = true;
            lastCalibSignalTime = millis(); // Продлеваем/сбрасываем таймаут
            Serial.println("[ESP-NOW] Получена команда СТАРТ калибровки.");
        } 
        else if (rxCalib.mode == STOP) 
        {
            calibration = false;
            stopSoilMoisture(); // Выключаем питание сенсора
            Serial.println("[ESP-NOW] Получена команда СТОП калибровки.");
        }
    }
    else if (type == SETTINGS_UPDATE) {
        Serial.println("Новые настройки");
        // 1. ЗАЩИТА: Проверяем, что пакет долетел полностью и не побит
        if (len < sizeof(struct_settings)) {
            Serial.println("[NET] Ошибка: Получен неполный пакет настроек. Отмена.");
            return;
        }

        // 2. РАДИОТИШИНА: Мгновенно отключаем флаг калибровки и гасим датчик.
        // Это самое важное! Как только мы сбросим флаг 'calibration', 
        // функция loop() ТУТ ЖЕ перестанет вызывать esp_now_send каждые 350 мс.
        // Эфир очищается, и мы сможем без коллизий отправить хосту ответный статус.
        if (calibration) {
            calibration = false;
            stopSoilMoisture();
            Serial.println("[NET] Режим калибровки остановлен для записи настроек.");
        }

        // 3. Копируем прилетевшие настройки
        struct_settings rxSettings;
        memcpy(&rxSettings, incomingData, sizeof(rxSettings));

        // 4. Готовим пакет ответа для веб-интерфейса хоста
        struct_settings_status statusPacket;
        statusPacket.msgType = SETTINGS_STATUS;
        WiFi.macAddress(statusPacket.macAddr);
        statusPacket.success = 1;
        statusPacket.errorCode = 0;

        // 5. Валидируем и записываем данные в Preferences (NVS флеш датчика)
        if (!saveCalibrationSettings(rxSettings.airValue, rxSettings.waterValue)) {
            statusPacket.success = 0; statusPacket.errorCode = 1;
        } else if (!saveSleepInterval(rxSettings.timeToSleepSec)) {
            statusPacket.success = 0; statusPacket.errorCode = 2;
        } else if (!saveWifiPower(rxSettings.wifiTxPower)) {
            statusPacket.success = 0; statusPacket.errorCode = 3;
        } else if (!saveMoistureThreshold(rxSettings.moistureThreshold)) {
            statusPacket.success = 0; statusPacket.errorCode = 4;
        } else if (!saveMaxSkippedBoots(rxSettings.maxSkippedBoots)) {
            statusPacket.success = 0; statusPacket.errorCode = 5;
        }

        // 6. Отправляем отчет о проделанной работе обратно на Хост.
        // Теперь пакет долетит со 100% вероятностью, так как датчик сам больше ничего не шлет.
        esp_now_send(hostMac, (uint8_t *)&statusPacket, sizeof(statusPacket));
        
        Serial.printf("[NET] Настройки успешно применены. Статус: %s (Код: %d)\n", 
                      statusPacket.success ? "УСПЕХ" : "ОШИБКА", statusPacket.errorCode);
        
        // (Опционально) Если датчик был разбужен кнопкой принудительно, 
        // после сохранения настроек можно скомандовать ему сразу спать, взведя флаг txDone.
        // txDone = true; 
    }
}

// Инициализация интерфейса связи
void initWiFiAndEspNow() {
    WiFi.mode(WIFI_STA);
    loadMacAddress();
    WiFi.setTxPower((wifi_power_t)r_wifiTxPower);

    if (esp_now_init() != ESP_OK) {
        Serial.println("Ошибка инициализации ESP-NOW");
        return;
    }

    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);

    // Добавляем широковещательный адрес
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    // Восстанавливаем пир Хоста, если уже сопряжены (память стирается при глубоком сне)
    if (pairingStatus == PAIRED) {
        memset(&peerInfo, 0, sizeof(peerInfo));
        memcpy(peerInfo.peer_addr, hostMac, 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
    }
}

void sendPairingRequest() {
    pairingData.msgType = PAIRING;
    pairingData.isReply = 0;
    WiFi.macAddress(pairingData.macAddr);

    DEBUG_PRINTLN("Поиск Хоста...");
    if (esp_now_send(broadcastAddress, (uint8_t *)&pairingData, sizeof(pairingData)) == ESP_OK) {
        pairingStatus = PAIRING_REQUESTED;
    }
}

void sendSensorData(float hum) {
    myData.msgType = DATA;
    WiFi.macAddress(myData.macAddr);
    myData.hum = hum;
    
    globalReadingId++; // Инкрементируем защищенный счетчик в RTC
    myData.readingId = globalReadingId;

    Serial.printf("Отправка данных на Хост. Влажность: %.2f%%, Пакет №: %u\n", myData.hum, myData.readingId);
    txDone = false;
    esp_now_send(hostMac, (uint8_t *)&myData, sizeof(myData));
}

void resetNetworkSettings() {
    DEBUG_PRINTLN("\n[RESET] >>> Запуск принудительного сброса настроек! <<<");

    if (pairingStatus == PAIRED || pairingStatus == PAIRING_REQUESTED) {
        esp_now_del_peer(hostMac);
    }

    pairingStatus = NOT_PAIRED; 
    memset(hostMac, 0, 6);   
    lastSentMoisture = -100.0f;   

    uint8_t randomMac[6];
    randomSeed(analogRead(0) + esp_random());
    for (int i = 0; i < 6; i++) {
        randomMac[i] = random(0, 256);
    }
    randomMac[0] = (randomMac[0] & 0xFE) | 0x02;

    if (esp_wifi_set_mac(WIFI_IF_STA, randomMac) == ESP_OK) {
        DEBUG_PRINT("[WIFI] Установлен новый случайный MAC: ");
        DEBUG_PRINTLN(WiFi.macAddress());
    }

    Preferences preferences;
    preferences.begin("wifi_cfg", false);
    preferences.putBytes("stored_mac", randomMac, 6);
    preferences.end();
}
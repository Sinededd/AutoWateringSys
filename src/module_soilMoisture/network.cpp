#include "network.h"
#include "config.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>

// Определение (выделение памяти) для RTC-переменных
RTC_DATA_ATTR PairingStatus pairingStatus = NOT_PAIRED;
RTC_DATA_ATTR uint8_t hostMac[6] = {0};
RTC_DATA_ATTR unsigned int globalReadingId = 0;

// Системные переменные сети
volatile bool txDone = false;
bool updateMAC = false;

static uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static struct_pairing pairingData;
static struct_message myData;

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

// Коллбэк отправки данных
static void OnDataSent(const wifi_tx_info_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("Status доставки пакета: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Успешно" : "Ошибка");
    txDone = true; 
}

// Коллбэк приема данных (сопряжение)
static void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
    if (len == 0 || incomingData[0] != PAIRING) return;

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
    }
}

// Инициализация интерфейса связи
void initWiFiAndEspNow() {
    WiFi.mode(WIFI_STA);
    loadMacAddress();
    WiFi.setTxPower(WIFI_POWER_8_5dBm);

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
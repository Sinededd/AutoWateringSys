// ============================================================================
// КОНФИГУРАЦИЯ И НАСТРОЙКИ
// ============================================================================
#define DEBUG_ENABLE

#ifdef DEBUG_ENABLE
    #define DEBUG_PRINT(x)     Serial.print(x)
    #define DEBUG_PRINTLN(x)   Serial.println(x)
    #define DEBUG_PRINTF(...)  Serial.printf(__VA_ARGS__)
#else
    #define DEBUG_PRINT(x)
    #define DEBUG_PRINTLN(x)
    #define DEBUG_PRINTF(...)
#endif

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <Button2.h>

// Аппаратные пины
constexpr uint8_t BUTTON_PIN = 3;
constexpr uint8_t LED_PIN    = 8;
constexpr uint8_t SENSOR_POWER_PIN  = 4;
constexpr uint8_t SENSOR_ANALOG_PIN = 1;

// Настройки калибровки датчика влажности
constexpr int AIR_VALUE   = 3100;
constexpr int WATER_VALUE = 1600;

// Интервалы и тайминги (в миллисекундах)
constexpr unsigned long SEND_INTERVAL    = 5000;
constexpr unsigned long PAIRING_INTERVAL = 3000;
constexpr unsigned long RESET_BLINK_TIME = 2500; // Время быстрого мигания при сбросе

// ============================================================================
// ТИПЫ ДАННЫХ И СТРУКТУРЫ
// ============================================================================
enum MessageType {
    PAIRING,
    DATA,
};

enum PairingStatus {
    NOT_PAIRED,
    PAIRING_REQUESTED,
    PAIRED
};

typedef struct struct_message {
    uint8_t msgType;
    uint8_t macAddr[6];
    float hum;
    unsigned int readingId;
} struct_message;

typedef struct struct_pairing {
    uint8_t msgType;
    uint8_t isReply; // 0 - запрос от датчика, 1 - ответ от хоста
    uint8_t macAddr[6];
} struct_pairing;

// ============================================================================
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ СОСТОЯНИЯ (Глобальный статус системы)
// ============================================================================
Button2 btn;
struct_message myData; 
struct_message inData; 
struct_pairing pairingData;

PairingStatus pairingStatus = NOT_PAIRED;
uint8_t hostMac[6];
uint8_t broadcastAddress[]  = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Переменные таймеров и индикации
unsigned long lastTransmission = 0;
unsigned long pairedTime       = 0;
unsigned long lastToggle       = 0;
unsigned long rapidBlinkTimer  = 0;

bool ledState     = true;
bool currentState = true;
bool updateMAC    = false; 

// ============================================================================
// СИСТЕМНЫЕ ФУНКЦИИ И СЕТЕВАЯ ЛОГИКА
// ============================================================================
void loadMacAddress() {
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

void setNewMacAddress() {
    DEBUG_PRINTLN("\n[RESET] >>> Запуск принудительного сброса настроек! <<<");

    if (pairingStatus == PAIRED || pairingStatus == PAIRING_REQUESTED) {
        if (esp_now_del_peer(hostMac) == ESP_OK) {
            DEBUG_PRINTLN("[ESP-NOW] Старый Хост удален из пиров.");
        }
    }

    pairingStatus = NOT_PAIRED; 
    memset(hostMac, 0, 6);      
    lastTransmission = 0;       

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
    if (preferences.isKey("stored_host_mac")) {
        preferences.remove("stored_host_mac");
    }
    preferences.end();
}

void updateLedIndicator() {
    unsigned long currentMillis = millis();

    // Режим быстрого моргания при смене MAC
    if (updateMAC && (currentMillis - lastToggle < RESET_BLINK_TIME)) {
        if (currentMillis - rapidBlinkTimer > 175) {
            rapidBlinkTimer = currentMillis;
            ledState = !ledState;
            digitalWrite(LED_PIN, ledState);
        }
        return;
    } else {
        if (updateMAC) { // Момент окончания анимации сброса
            updateMAC = false;
            lastToggle = currentMillis; // Сбрасываем таймер для обычного мигания
        }
    }

    // Стандартные режимы работы
    switch (pairingStatus) {
        case NOT_PAIRED:
        case PAIRING_REQUESTED:
            if (currentMillis - lastToggle >= 1000) {
                ledState = !ledState;
                lastToggle = currentMillis;
            }
            break;

        case PAIRED:
            ledState = (currentMillis - pairedTime > 15000);
            break;
    }

    if (currentState != ledState) {
        currentState = ledState;
        digitalWrite(LED_PIN, currentState);
    }
}

// ============================================================================
// ОБРАБОТЧИКИ СОБЫТИЙ (CALLBACKS)
// ============================================================================
void OnDataSent(const wifi_tx_info_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("Status доставки пакета: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Успешно" : "Ошибка");
}

void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
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
        pairedTime = millis();
    }
}

void handleButtonTap(Button2 &b) {
    Serial.println("Клик!");
}

void handleLongPressDetected(Button2 &b) {
    Serial.println("[BUTTON] Удержание сработало! Меняем конфигурацию...");
    setNewMacAddress();
    updateMAC = true;
    lastToggle = millis();
}

// ============================================================================
// ФУНКЦИЯ ЧТЕНИЯ ДАТЧИКА С УПРАВЛЕНИЕМ ПИТАНИЕМ
// ============================================================================
float readSoilMoisture() {
    // 1. Включаем питание датчика
    digitalWrite(SENSOR_POWER_PIN, HIGH);
    
    // 2. Даем датчику время проснуться (твой рабочий 1-секундный интервал)
    delay(1000); 
    
    // 3. Делаем несколько замеров в милливольтах для сглаживания шумов
    long analogMilliVolts = 0;
    for (int i = 0; i < 5; i++) {
        analogMilliVolts += analogReadMilliVolts(SENSOR_ANALOG_PIN); // <--- Читаем мВ!
        delay(5);
    }
    analogMilliVolts /= 5; // Среднее арифметическое в милливольтах

    // 4. Мгновенно выключаем датчик
    digitalWrite(SENSOR_POWER_PIN, LOW);

    // Выводим в дебаг реальные милливольты
    DEBUG_PRINTF("[SENSOR] Напряжение на пине: %d мВ\n", analogMilliVolts);

    // 5. Переводим милливольты в проценты влажности
    int moisturePercent = map(analogMilliVolts, AIR_VALUE, WATER_VALUE, 0, 100);
    moisturePercent = constrain(moisturePercent, 0, 100);

    return (float)moisturePercent;
}

// ============================================================================
// ОСНОВНЫЕ ФУНКЦИИ ЯДРА ARDUINO (setup / loop)
// ============================================================================
void setup() {
#ifdef DEBUG_ENABLE
    Serial.begin(115200);
    delay(1000);
#endif

    pinMode(LED_PIN, OUTPUT);
    pinMode(SENSOR_POWER_PIN, OUTPUT);
    digitalWrite(SENSOR_POWER_PIN, LOW);

    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);

    btn.begin(BUTTON_PIN);
    btn.setLongClickTime(5000);
    btn.setTapHandler(handleButtonTap); // Вернул обработчик клика, раз функция была в коде
    btn.setLongClickDetectedHandler(handleLongPressDetected);

    WiFi.mode(WIFI_STA);
    loadMacAddress();
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    Serial.print("Мой MAC-адрес: ");
    Serial.println(WiFi.macAddress());

    if (esp_now_init() != ESP_OK) {
        Serial.println("Ошибка инициализации ESP-NOW");
        return;
    }

    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);

    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Не удалось добавить широковещательный адрес.");
    }
}

void loop() {
    btn.loop();
    updateLedIndicator();
    unsigned long currentMillis = millis();

    switch (pairingStatus) {
        case NOT_PAIRED: {
            if (currentMillis - lastTransmission >= PAIRING_INTERVAL) {
                lastTransmission = currentMillis;
                pairingData.msgType = PAIRING;
                pairingData.isReply = 0;
                WiFi.macAddress(pairingData.macAddr);

                DEBUG_PRINTLN("Отправка запроса на сопряжение (Broadcast)...");
                if (esp_now_send(broadcastAddress, (uint8_t *)&pairingData, sizeof(pairingData)) == ESP_OK) {
                    pairingStatus = PAIRING_REQUESTED;
                }
            }
            break;
        }

        case PAIRING_REQUESTED: {
            if (currentMillis - lastTransmission >= PAIRING_INTERVAL) {
                Serial.println("Превышено время ожидания ответа. Повтор...");
                pairingStatus = NOT_PAIRED;
            }
            break;
        }

        case PAIRED: {
            if (currentMillis - lastTransmission >= SEND_INTERVAL) {
                lastTransmission = currentMillis;
                myData.msgType = DATA;
                WiFi.macAddress(myData.macAddr);
                myData.hum = readSoilMoisture();
                myData.readingId++;

                Serial.printf("Отправка данных на Хост. Влажность: %.2f%%, Пакет №: %u\n", myData.hum, myData.readingId);
                esp_now_send(hostMac, (uint8_t *)&myData, sizeof(myData));
            }
            break;
        }
    }
}
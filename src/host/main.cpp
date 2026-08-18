#define DEBUG_ENABLE

#ifdef DEBUG_ENABLE
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTF(...)
#endif

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <esp_now.h>
#include <ArduinoJson.h>
#include "storage.h"
#include "web_server.h"
#include "../common/types.h"
#include "Logger.h"
#include "Config.h"
#include <Led.h>
#include <common/network/EspNowManager.h>

// PIN Settings
const uint8_t LED_PIN = 2;
const uint8_t VALVE_PIN = 4;

struct_message incomingReadings;
struct_message outgoingSetpoints;
struct_pairing pairingData;

extern AsyncEventSource events;

const char *ssid = "ESP32-HOST";
bool ledActive = true;

bool isDeviceAlive(size_t index)
{
    if (deviceLastSeenMs[index] == 0)
        return false; // Датчик еще ни разу не выходил на связь

    // Формула таймаута: период сна * макс пропусков. Добавим запас 5 секунд на девиацию таймеров
    uint32_t heartbeatTimeoutMs = (pairedDevices[index].timeToSleepSec * pairedDevices[index].maxSkippedBoots * 1000) + 5000;

    return (millis() - deviceLastSeenMs[index]) < heartbeatTimeoutMs;
}

bool checkWateringTrigger()
{
    int aliveCount = 0;
    float sumHumidity = 0;
    float minHumidity = 200.0f; // Инициализируем заведомо большим числом

    for (size_t i = 0; i < deviceCount; i++)
    {
        if (isDeviceAlive(i) && deviceLastHumidity[i] >= 0)
        {
            aliveCount++;
            sumHumidity += deviceLastHumidity[i];
            if (deviceLastHumidity[i] < minHumidity)
            {
                minHumidity = deviceLastHumidity[i];
            }
        }
    }

    if (aliveCount == 0)
    {
        // Если все датчики мертвы, полив не включаем в целях безопасности (чтобы не затопить)
        return false;
    }

    if (currentWateringMode == MODE_AVERAGE)
    {
        float avgHumidity = sumHumidity / aliveCount;
        return avgHumidity < globalMoistureThreshold;
    }
    else if (currentWateringMode == MODE_MINIMUM)
    {
        return minHumidity < globalMoistureThreshold;
    }

    return false;
}

void handleWateringLogic()
{
    switch (currentValveState)
    {
    case VALVE_IDLE:
        // Проверяем условия полива раз в секунду (или чаще)
        static uint32_t lastCheck = 0;
        if (millis() - lastCheck > 2000)
        {
            lastCheck = millis();
            if (checkWateringTrigger())
            {
                DEBUG_PRINTLN("[VALVE] Влажность ниже нормы! Включение полива.");
                digitalWrite(VALVE_PIN, HIGH);
                digitalWrite(LED_PIN, HIGH);
                currentValveState = VALVE_WATERING;
                stateTimerMs = millis();
            }
        }
        break;

    case VALVE_WATERING:
        if (millis() - stateTimerMs >= wateringDurationMs)
        {
            DEBUG_PRINTLN("[VALVE] Время полива истекло. Отключение, переход в режим простоя.");
            digitalWrite(VALVE_PIN, LOW);
            digitalWrite(LED_PIN, LOW);
            currentValveState = VALVE_COOLDOWN;
            stateTimerMs = millis();
        }
        break;

    case VALVE_COOLDOWN:
        if (millis() - stateTimerMs >= cooldownDurationMs)
        {
            DEBUG_PRINTLN("[VALVE] Время обязательного простоя выждано. Переход в режим ожидания.");
            currentValveState = VALVE_IDLE;
        }
        break;
    }
}

// Вспомогательная функция для конвертации строки "AA:BB:CC..." в массив байт
void parseMacAddress(const char *macStr, uint8_t *macBytes)
{
    int values[6];
    if (sscanf(macStr, "%X:%X:%X:%X:%X:%X", &values[0], &values[1], &values[2], &values[3], &values[4], &values[5]) == 6)
    {
        for (int i = 0; i < 6; ++i)
        {
            macBytes[i] = (uint8_t)values[i];
        }
    }
}

void printMAC(const uint8_t *mac_addr)
{
    Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X",
                  mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
}

void OnDataSent(const wifi_tx_info_t *mac_addr, esp_now_send_status_t status)
{
    char macStr[18];
    DEBUG_PRINT("Last Packet Send Status: ");
    DEBUG_PRINT(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success to " : "Delivery Fail to ");
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    DEBUG_PRINT(macStr);
    DEBUG_PRINTLN();
}

bool addPeer(const uint8_t *mac_addr)
{
    if (esp_now_is_peer_exist(mac_addr))
    {
        return true;
    }

    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, mac_addr, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_AP;

    esp_err_t addStatus = esp_now_add_peer(&peerInfo);
    if (addStatus == ESP_OK)
    {
        DEBUG_PRINTLN("[ESP-NOW] Устройство успешно добавлено в список пиров.");
        return true;
    }
    else
    {
        DEBUG_PRINTF("[ESP-NOW] Ошибка добавления пира! Код: 0x%X\n", addStatus);
        return false;
    }
}

bool deletePeer(const uint8_t *mac_addr)
{
    if (!esp_now_is_peer_exist(mac_addr))
    {
        DEBUG_PRINTLN("[ESP-NOW] Такого пира нет в системе.");
        return false;
    }

    esp_err_t delStatus = esp_now_del_peer(mac_addr);
    if (delStatus == ESP_OK)
    {
        DEBUG_PRINTLN("[ESP-NOW] Пир успешно удален из памяти ESP-NOW.");
        return true;
    }
    else
    {
        DEBUG_PRINTF("[ESP-NOW] Ошибка удаления пира! Код: 0x%X\n", delStatus);
        return false;
    }
}

void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len)
{
    if (len == 0)
        return;

    int rssi = recv_info->rx_ctrl->rssi;
    uint8_t type = incomingData[0];

    switch (type)
    {
    case DATA:
    {
        memcpy(&incomingReadings, incomingData, sizeof(incomingReadings));

        uint8_t localClientMac[6];
        memcpy(localClientMac, incomingReadings.macAddr, 6);

        for (size_t i = 0; i < deviceCount; i++)
        {
            if (memcmp(pairedDevices[i].mac, localClientMac, 6) == 0)
            {
                deviceLastSeenMs[i] = millis();
                deviceLastHumidity[i] = incomingReadings.hum;
                break;
            }
        }

        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 localClientMac[0], localClientMac[1], localClientMac[2],
                 localClientMac[3], localClientMac[4], localClientMac[5]);

        JsonDocument root;
        root["mac"] = macStr;
        root["humidity"] = incomingReadings.hum;
        root["readingId"] = String(incomingReadings.readingId);
        root["rssi"] = rssi;

        String payload;
        serializeJson(root, payload);

        events.send(payload.c_str(), "new_readings", millis());
        break;
    }

    case PAIRING:
    {
        memcpy(&pairingData, incomingData, sizeof(pairingData));

        if (pairingData.isReply != 0)
            break;

        uint8_t localClientMac[6];
        memcpy(localClientMac, pairingData.macAddr, 6);

        // Проверяем, авторизовано ли уже устройство ранее
        bool isAlreadyPaired = false;
        for (size_t i = 0; i < deviceCount; i++)
        {
            if (memcmp(pairedDevices[i].mac, localClientMac, 6) == 0)
            {
                isAlreadyPaired = true;
                break;
            }
        }

        // Если устройство уже в белом списке — автоотвечаем ему
        if (isAlreadyPaired)
        {
            addPeer(localClientMac);
            struct_pairing reply;
            reply.msgType = PAIRING;
            reply.isReply = 1;
            WiFi.softAPmacAddress(reply.macAddr);
            esp_now_send(localClientMac, (uint8_t *)&reply, sizeof(reply));
            DEBUG_PRINTLN("[ESP-NOW] Авто-ответ сопряженному устройству.");
        }
        // Если устройство новое — отправляем запрос на веб-страницу для ручного подтверждения
        else
        {
            char macStr[18];
            snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                     localClientMac[0], localClientMac[1], localClientMac[2],
                     localClientMac[3], localClientMac[4], localClientMac[5]);

            JsonDocument pairingLog;
            pairingLog["mac"] = macStr;
            pairingLog["rssi"] = rssi;
            String pairingPayload;
            serializeJson(pairingLog, pairingPayload);

            events.send(pairingPayload.c_str(), "pairing_request", millis());
            DEBUG_PRINTLN("[ESP-NOW] Новый запрос отправлен в Web-интерфейс.");
        }
        break;
    }

    case SETTINGS_REPORT:
    {
        struct_settings rxSettings;
        memcpy(&rxSettings, incomingData, sizeof(rxSettings));

        // Ищем устройство в базе и обновляем конфигурацию
        for (size_t i = 0; i < deviceCount; i++)
        {
            if (memcmp(pairedDevices[i].mac, rxSettings.macAddr, 6) == 0)
            {
                pairedDevices[i].airValue = rxSettings.airValue;
                pairedDevices[i].waterValue = rxSettings.waterValue;
                pairedDevices[i].timeToSleepSec = rxSettings.timeToSleepSec;
                pairedDevices[i].wifiTxPower = rxSettings.wifiTxPower;
                pairedDevices[i].moistureThreshold = rxSettings.moistureThreshold;
                pairedDevices[i].maxSkippedBoots = rxSettings.maxSkippedBoots;

                saveDevicesToStorage(); // Сохраняем обновленную структуру в NVS флеш хоста

                // Отправляем SSE-уведомление на Web-страницу
                char macStr[18];
                snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                         rxSettings.macAddr[0], rxSettings.macAddr[1], rxSettings.macAddr[2],
                         rxSettings.macAddr[3], rxSettings.macAddr[4], rxSettings.macAddr[5]);

                JsonDocument doc;
                doc["mac"] = macStr;
                doc["air"] = rxSettings.airValue;
                doc["water"] = rxSettings.waterValue;
                doc["sleep"] = rxSettings.timeToSleepSec;
                doc["wifi"] = rxSettings.wifiTxPower;
                doc["threshold"] = rxSettings.moistureThreshold;
                doc["max_skip"] = rxSettings.maxSkippedBoots;

                String payload;
                serializeJson(doc, payload);
                events.send(payload.c_str(), "settings_report", millis());
                DEBUG_PRINTLN("[ESP-NOW] Настройки датчика успешно сохранены и отправлены в Web.");
                break;
            }
        }
        break;
    }

    case SETTINGS_STATUS:
    {
        struct_settings_status rxStatus;
        memcpy(&rxStatus, incomingData, sizeof(rxStatus));

        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 rxStatus.macAddr[0], rxStatus.macAddr[1], rxStatus.macAddr[2],
                 rxStatus.macAddr[3], rxStatus.macAddr[4], rxStatus.macAddr[5]);

        JsonDocument doc;
        doc["mac"] = macStr;
        doc["success"] = rxStatus.success;
        doc["errorCode"] = rxStatus.errorCode;

        String payload;
        serializeJson(doc, payload);
        // Шлем событие на фронтенд, фронт покажет "Успешно сохранено" или всплывающую ошибку
        events.send(payload.c_str(), "settings_status", millis());
        DEBUG_PRINTLN("[ESP-NOW] Получен статус применения настроек от удаленного датчика.");
        break;
    }

    case CALIB:
    {
        struct_calib rxCalib;
        memcpy(&rxCalib, incomingData, sizeof(rxCalib));

        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 rxCalib.macAddr[0], rxCalib.macAddr[1], rxCalib.macAddr[2],
                 rxCalib.macAddr[3], rxCalib.macAddr[4], rxCalib.macAddr[5]);

        // Валидация входящего значения мВ
        if (rxCalib.volts < 0 || rxCalib.volts > 4095)
        {
            DEBUG_PRINTF("[CALIB] ОШИБКА: Некорректное значение мВ от датчика: %d\n", rxCalib.volts);
            break;
        }

        JsonDocument doc;
        doc["mac"] = macStr;
        doc["volts"] = rxCalib.volts; // Сырые милливольты от датчика

        String payload;
        serializeJson(doc, payload);

        // Отправляем событие в браузер на страницу калибровки
        events.send(payload.c_str(), "calib_stream", millis());
        break;
    }
    }
}

void initESP_NOW()
{
    if (esp_now_init() != ESP_OK)
    {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);

    loadDevicesFromStorage();
    if (deviceCount > 0)
    {
        Serial.println("Restore connections ESP-NOW...");
        for (size_t i = 0; i < deviceCount; i++)
        {
            addPeer(pairedDevices[i].mac);
        }
    }
}

static const char *TAG = "Main";

Led led(Config::Pins::LED_PIN, false);
EspNowManager network();

void setup()
{
    Logger::begin();
    LOG_I(TAG, "Start...");

    led.begin();

    // if (!LittleFS.begin(true))
    // {
    //     LOG_E(TAG, "Ошибка LittleFS!");
    //     return;
    // }

    // pinMode(LED_PIN, OUTPUT);
    // digitalWrite(LED_PIN, LOW);

    // pinMode(VALVE_PIN, OUTPUT);
    // digitalWrite(VALVE_PIN, LOW);

    // loadWateringSettingsFromStorage();

    // WiFi.mode(WIFI_AP);
    // WiFi.softAP("ESP32-HOST");

    // initESP_NOW();   // Инициализируем сеть
    // initWebServer(); // Запускаем веб-сервер одной строчкой!
}

void loop()
{
    // handleWateringLogic();

    led.update();
}
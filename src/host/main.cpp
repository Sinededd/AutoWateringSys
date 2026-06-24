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

// PIN Settings
const uint8_t LED_PIN = 2;


enum MessageType
{
    PAIRING,
    DATA,
};

typedef struct struct_message
{
    uint8_t msgType;
    uint8_t macAddr[6];
    float hum;
    unsigned int readingId;
} struct_message;

typedef struct struct_pairing
{
    uint8_t msgType;
    uint8_t isReply; // 0 - запрос от датчика, 1 - ответ от хоста
    uint8_t macAddr[6];
} struct_pairing;

struct_message incomingReadings;
struct_message outgoingSetpoints;
struct_pairing pairingData;

AsyncWebServer server(80);
AsyncEventSource events("/events");

const char *ssid = "ESP32-HOST";
bool ledActive = true;

// Вспомогательная функция для конвертации строки "AA:BB:CC..." в массив байт
void parseMacAddress(const char* macStr, uint8_t* macBytes) {
    int values[6];
    if (sscanf(macStr, "%X:%X:%X:%X:%X:%X", &values[0], &values[1], &values[2], &values[3], &values[4], &values[5]) == 6) {
        for (int i = 0; i < 6; ++i) {
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

        if (pairingData.isReply != 0) break;

        uint8_t localClientMac[6];
        memcpy(localClientMac, pairingData.macAddr, 6);

        // Проверяем, авторизовано ли уже устройство ранее
        bool isAlreadyPaired = false;
        for (size_t i = 0; i < deviceCount; i++) {
            if (memcmp(pairedDevices[i], localClientMac, 6) == 0) {
                isAlreadyPaired = true;
                break;
            }
        }

        // Если устройство уже в белом списке — автоотвечаем ему
        if (isAlreadyPaired) {
            addPeer(localClientMac);
            struct_pairing reply;
            reply.msgType = PAIRING;
            reply.isReply = 1;
            WiFi.softAPmacAddress(reply.macAddr);
            esp_now_send(localClientMac, (uint8_t *)&reply, sizeof(reply));
            DEBUG_PRINTLN("[ESP-NOW] Авто-ответ сопряженному устройству.");
        } 
        // Если устройство новое — отправляем запрос на веб-страницу для ручного подтверждения
        else {
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
            addPeer(pairedDevices[i]);
        }
    }
}

void setup()
{
#ifdef DEBUG_ENABLE
    Serial.begin(115200);
    delay(1000);
#endif

    if (!LittleFS.begin(true))
    {
        DEBUG_PRINTLN("Ошибка при монтировании LittleFS!");
        return;
    }

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, ledActive ? HIGH : LOW);

    WiFi.mode(WIFI_AP);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.softAP(ssid);

    initESP_NOW();

    server.addHandler(&events);

    // НОВЫЙ ЭНДПОИНТ: Обработка клика по кнопке «Подключить»
    server.on("/connect", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("mac")) {
            String macStr = request->getParam("mac")->value();
            uint8_t targetMac[6];
            parseMacAddress(macStr.c_str(), targetMac);

            // 1. Сохраняем устройство в NVS/файловую систему через storage.h
            addDevice(targetMac); 
            // 2. Регистрируем как пира ESP-NOW
            addPeer(targetMac);

            // 3. Отправляем ответ подтверждения (на случай если датчик прямо сейчас слушает эфир)
            struct_pairing reply;
            reply.msgType = PAIRING;
            reply.isReply = 1;
            WiFi.softAPmacAddress(reply.macAddr);
            esp_now_send(targetMac, (uint8_t *)&reply, sizeof(reply));

            DEBUG_PRINTF("[WEB] Устройство %s успешно одобрено пользователем.\n", macStr.c_str());
            request->send(200, "text/plain", "Устройство добавлено");
        } else {
            request->send(400, "text/plain", "Неверный запрос (нет параметра mac)");
        }
    });

    server.on("/devices", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        
        for (size_t i = 0; i < deviceCount; i++) {
            char macStr[18];
            snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                     pairedDevices[i][0], pairedDevices[i][1], pairedDevices[i][2],
                     pairedDevices[i][3], pairedDevices[i][4], pairedDevices[i][5]);
            arr.add(macStr);
        }
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        if (LittleFS.exists("/index.html")) {
            request->send(LittleFS, "/index.html", "text/html");
        } else {
            request->send(404, "text/plain", "Файл /index.html НЕ НАЙДЕН в LittleFS.");
        } 
    });

    server.begin();
    DEBUG_PRINTLN("HTTP веб-сервер успешно запущен.");
}

void loop()
{
}
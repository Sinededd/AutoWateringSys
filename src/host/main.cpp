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
const int ledPin = 8;
bool ledActive = false;

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
        DEBUG_PRINTLN("[ESP-NOW] Устройство уже зарегистрировано в системе.");
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
        DEBUG_PRINTF("[ESP-NOW] Ошибка добавления устройства! Код: 0x%X\n", addStatus);
        return false;
    }
}

void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len)
{
    if (len == 0)
        return;

    int rssi = recv_info->rx_ctrl->rssi;

    DEBUG_PRINTF("[ESP-NOW] Received %d bytes from receiver.\n", len);
    uint8_t type = incomingData[0];

    switch (type)
    {
    case DATA:
    {
        memcpy(&incomingReadings, incomingData, sizeof(incomingReadings));

        // get string of MAC
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

#ifdef DEBUG_ENABLE
        Serial.println("Event send: ");
        serializeJson(root, Serial);
        Serial.println();
#endif

        events.send(payload.c_str(), "new_readings", millis());
        break;
    }

    case PAIRING:
    {
        memcpy(&pairingData, incomingData, sizeof(pairingData));

        if (pairingData.isReply != 0)
        {
            break;
        }

        DEBUG_PRINT("Pairing request from MAC Address: ");
#ifdef DEBUG_ENABLE
        printMAC(pairingData.macAddr);
#endif
        DEBUG_PRINTLN();

        // get mac of sender
        uint8_t localClientMac[6];
        memcpy(localClientMac, pairingData.macAddr, 6);
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

        // Сохранение датчика в памяти, и отправка ему данных сервера

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
        Serial.println("Ошибка при монтировании LittleFS!");
        return;
    }

    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, ledActive ? LOW : HIGH);

    WiFi.mode(WIFI_AP);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.softAP(ssid);

    DEBUG_PRINTLN("");
    DEBUG_PRINT("Открытая точка доступа запущена: ");
    DEBUG_PRINTLN(ssid);
    DEBUG_PRINT("IP-адрес сервера: ");
    DEBUG_PRINTLN(WiFi.softAPIP().toString());

    initESP_NOW();

    server.addHandler(&events);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              {
    if (LittleFS.exists("/index.html")) {
        request->send(LittleFS, "/index.html", "text/html");
    } else {
        request->send(404, "text/plain", "Файл /index.html НЕ НАЙДЕН в памяти! Проверьте загрузку LittleFS.");
    } });

    server.begin();
    DEBUG_PRINTLN("HTTP веб-сервер успешно запущен.");
}

void loop()
{
}
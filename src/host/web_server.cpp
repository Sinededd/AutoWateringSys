#include "web_server.h"
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "storage.h"

// 1. Создаем объекты сервера внутри этого модуля
AsyncWebServer server(80);
AsyncEventSource events("/events");

// 2. Говорим, что эти переменные и функции лежат в других файлах (main или esp_now)
extern PairedDevice pairedDevices[];
extern size_t deviceCount;
extern bool addPeer(const uint8_t *mac_addr);
extern bool deletePeer(const uint8_t *mac_addr);
extern void parseMacAddress(const char *macStr, uint8_t *macBytes);

// 3. Логика маршрутов
void initWebServer()
{
    server.addHandler(&events);

    // Главная страница
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        if (LittleFS.exists("/index.html")) {
            request->send(LittleFS, "/index.html", "text/html");
        } else {
            request->send(404, "text/plain", "Файл /index.html не найден.");
        } });

    // Маршрут подключения
    server.on("/connect", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        if (request->hasParam("mac")) {
            String macStr = request->getParam("mac")->value();
            uint8_t targetMac[6];
            parseMacAddress(macStr.c_str(), targetMac);

            addDevice(targetMac); 
            addPeer(targetMac);

            request->send(200, "text/plain", "Устройство добавлено");
        } else {
            request->send(400, "text/plain", "Bad Request");
        } });

    server.on("/rename", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        if (request->hasParam("mac") && request->hasParam("name")) {
            String macStr = request->getParam("mac")->value();
            String nameStr = request->getParam("name")->value();
            uint8_t targetMac[6];
            parseMacAddress(macStr.c_str(), targetMac);

            if (renameDevice(targetMac, nameStr.c_str())) {
                request->send(200, "text/plain", "Имя успешно изменено");
            } else {
                request->send(404, "text/plain", "Устройство не найдено");
            }
        } else {
            request->send(400, "text/plain", "Bad Request");
        } });

    // Маршрут удаления
    server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        if (request->hasParam("mac")) {
            String macStr = request->getParam("mac")->value();
            uint8_t targetMac[6];
            parseMacAddress(macStr.c_str(), targetMac);

            deletePeer(targetMac); 

            if (deleteDevice(targetMac)) {
                request->send(200, "text/plain", "Устройство удалено");
            } else {
                request->send(404, "text/plain", "Не найдено в памяти");
            }
        } else {
            request->send(400, "text/plain", "Missing MAC");
        } });

    // Список устройств для фронтенда
    server.on("/devices", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        
        for (size_t i = 0; i < deviceCount; i++) {
            JsonObject obj = arr.add<JsonObject>();
            char macStr[18];
            snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                     pairedDevices[i].mac[0], pairedDevices[i].mac[1], pairedDevices[i].mac[2],
                     pairedDevices[i].mac[3], pairedDevices[i].mac[4], pairedDevices[i].mac[5]);
            obj["mac"] = macStr;
            obj["name"] = String(pairedDevices[i].name);
        }
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response); });

    server.begin();
    Serial.println("[Web] HTTP веб-сервер успешно запущен.");
}

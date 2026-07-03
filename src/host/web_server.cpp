#include "web_server.h"
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "storage.h"
#include <common/types.h>
#include <esp_now.h>

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
        
        // НОВЫЕ ПОЛЯ ДЛЯ ОТОБРАЖЕНИЯ НА СТРАНИЦЕ
        obj["air"] = pairedDevices[i].airValue;
        obj["water"] = pairedDevices[i].waterValue;
        obj["sleep"] = pairedDevices[i].timeToSleepSec;
        obj["wifi"] = pairedDevices[i].wifiTxPower;
        obj["threshold"] = pairedDevices[i].moistureThreshold;
        obj["max_skip"] = pairedDevices[i].maxSkippedBoots;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response); });

    server.on("/update_settings", HTTP_GET, [](AsyncWebServerRequest *request)
              {
    if (request->hasParam("mac")) {
        String macStr = request->getParam("mac")->value();
        uint8_t targetMac[6];
        parseMacAddress(macStr.c_str(), targetMac);

        struct_settings txSettings;
        txSettings.msgType = SETTINGS_UPDATE;
        memcpy(txSettings.macAddr, targetMac, 6);
        
        // Парсим параметры из GET запроса URL (если какого-то нет — ставим 0 или дефолт)
        txSettings.airValue          = request->hasParam("air") ? request->getParam("air")->value().toInt() : 2300;
        txSettings.waterValue        = request->hasParam("water") ? request->getParam("water")->value().toInt() : 1200;
        txSettings.timeToSleepSec    = request->hasParam("sleep") ? request->getParam("sleep")->value().toInt() : 600;
        txSettings.wifiTxPower       = request->hasParam("wifi") ? request->getParam("wifi")->value().toInt() : 40;
        txSettings.moistureThreshold = request->hasParam("threshold") ? request->getParam("threshold")->value().toInt() : 2;
        txSettings.maxSkippedBoots   = request->hasParam("max_skip") ? request->getParam("max_skip")->value().toInt() : 3;

        // Пуляем пакет в эфир напрямую конкретному датчику
        esp_err_t result = esp_now_send(targetMac, (uint8_t *)&txSettings, sizeof(txSettings));
        
        if (result == ESP_OK) {
            request->send(200, "text/plain", "Пакет отправлен в эфир. Ожидание ответа от датчика...");
        } else {
            request->send(500, "text/plain", "Ошибка отправки ESP-NOW");
        }
    } else {
        request->send(400, "text/plain", "Bad Request: Missing MAC");
    } });

    server.on("/calibration", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        if (request->hasParam("mac"))
        {
            String macStr = request->getParam("mac")->value();
            uint8_t targetMac[6];
            parseMacAddress(macStr.c_str(), targetMac);

            struct_calib calibration;
            calibration.msgType = CALIB;
            memcpy(calibration.macAddr, targetMac, 6);

            // Parse parameters fom GET of URL request
            // 0 - START, 1 - STOP
            calibration.mode = request->hasParam("mode") ? request->getParam("mode")->value().toInt() : calib_mode::STOP;

            Serial.printf("[CALIB] Отправлен запрос %s датчику %s\n", 
                          calibration.mode == START ? "START" : "STOP", macStr.c_str());

            esp_err_t result = esp_now_send(targetMac, (uint8_t *)&calibration, sizeof(calibration));

            if (result == ESP_OK)
            {
                request->send(200, "text/plain", "Переключение режима на калибровку. Ожидание ответа от датчика...");
            }
            else
            {
                request->send(500, "text/plain", "Ошибка отправки ESP-NOW");
            }
        }
        else
        {
            request->send(400, "text/plain", "Bad Request: Missing MAC");
        } });

    server.begin();
    Serial.println("[Web] HTTP веб-сервер успешно запущен.");
}

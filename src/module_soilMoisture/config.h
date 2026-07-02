#pragma once

#include <Arduino.h>
#include <WiFi.h>

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

// Аппаратные пины (СТАТИКА, НЕ МЕНЯЕМ)
constexpr uint8_t BUTTON_PIN        = 3;
constexpr uint8_t LED_PIN           = 8;
constexpr uint8_t SENSOR_POWER_PIN  = 10;
constexpr uint8_t SENSOR_ANALOG_PIN = 1;

// Значения по умолчанию (используются при первом запуске чипа)
constexpr int DEFAULT_AIR_VALUE           = 2300;
constexpr int DEFAULT_WATER_VALUE         = 1200;
constexpr uint64_t DEFAULT_TIME_TO_SLEEP_SEC = 7; 
constexpr int DEFAULT_WIFI_POWER          = WIFI_POWER_8_5dBm;
constexpr int DEFAULT_MOISTURE_THRESHOLD     = 2;
constexpr int DEFAULT_MAX_SKIPPED_BOOTS = 3;    // Сколько циклов сна плата может пропустить перед отправкой Heartbeat

// Интервалы интерфейса
constexpr unsigned long PAIRING_INTERVAL = 3000;
constexpr unsigned long RESET_BLINK_TIME = 2500;
#pragma once

#include <Arduino.h>

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

// Аппаратные пины
constexpr uint8_t BUTTON_PIN = 3;
constexpr uint8_t LED_PIN    = 8;
constexpr uint8_t SENSOR_POWER_PIN  = 10;
constexpr uint8_t SENSOR_ANALOG_PIN = 1;

// Настройки калибровки датчика влажности
constexpr int AIR_VALUE   = 3100;
constexpr int WATER_VALUE = 1600;

// Интервалы (в секундах)
constexpr uint64_t TIME_TO_SLEEP_SEC = 15; 

// Интервалы (в миллисекундах)
constexpr unsigned long PAIRING_INTERVAL = 3000;
constexpr unsigned long RESET_BLINK_TIME = 2500; 


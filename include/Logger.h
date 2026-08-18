#pragma once
#include <Arduino.h>

// Logging level
#define LOG_LEVEL_NONE  0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_INFO  3
#define LOG_LEVEL_DEBUG 4

// We set the INFO level by default if it is not specified in platformio.ini.
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

class Logger {
public:
    static void begin(unsigned long baud = 115200) {
#if (LOG_LEVEL > LOG_LEVEL_NONE)
        Serial.begin(baud);
        while (!Serial && millis() < 2000);
#endif
    }
};


#if (LOG_LEVEL >= LOG_LEVEL_ERROR)
    #define LOG_E(tag, fmt, ...) Serial.printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#else
    #define LOG_E(tag, fmt, ...) ((void)0)
#endif

#if (LOG_LEVEL >= LOG_LEVEL_WARN)
    #define LOG_W(tag, fmt, ...) Serial.printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#else
    #define LOG_W(tag, fmt, ...) ((void)0)
#endif

#if (LOG_LEVEL >= LOG_LEVEL_INFO)
    #define LOG_I(tag, fmt, ...) Serial.printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#else
    #define LOG_I(tag, fmt, ...) ((void)0)
#endif

#if (LOG_LEVEL >= LOG_LEVEL_DEBUG)
    #define LOG_D(tag, fmt, ...) Serial.printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__)
#else
    #define LOG_D(tag, fmt, ...) ((void)0)
#endif
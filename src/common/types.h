#pragma once

#include <Arduino.h>

enum MessageType { 
    PAIRING, 
    DATA, 
    SETTINGS_REPORT,  // Датчик -> Хост (текущие настройки датчика)
    SETTINGS_UPDATE,  // Хост -> Датчик (новые настройки с сервера)
    SETTINGS_STATUS   // Датчик -> Хост (результат применения: Ок / Ошибка)
};
enum PairingStatus { NOT_PAIRED, PAIRING_REQUESTED, PAIRED };

typedef struct struct_message {
    uint8_t msgType;
    uint8_t macAddr[6];
    float hum;
    unsigned int readingId;
} struct_message;

typedef struct struct_pairing {
    uint8_t msgType;
    uint8_t isReply; 
    uint8_t macAddr[6];
} struct_pairing;

// Структура пакета настроек
typedef struct struct_settings {
    uint8_t msgType; // SETTINGS_REPORT или SETTINGS_UPDATE
    uint8_t macAddr[6];
    int airValue;
    int waterValue;
    uint32_t timeToSleepSec;
    int wifiTxPower;
    int moistureThreshold;
    int maxSkippedBoots;
} struct_settings;

// Структура пакета статуса
typedef struct struct_settings_status {
    uint8_t msgType; // SETTINGS_STATUS
    uint8_t macAddr[6];
    uint8_t success;   // 1 - Успешно, 0 - Ошибка
    uint8_t errorCode; // 0 - Нет, 1 - Калибровка, 2 - Время сна, 3 - WiFi, 4 - Порог, 5 - Пульс
} struct_settings_status;

#pragma once

#include <Arduino.h>

#define MAX_DEVICES 20 


struct PairedDevice {
    uint8_t mac[6];
    char name[32]; // 31 символ + нуль-терминатор 
    
    // Добавленные поля настроек датчика
    int airValue;
    int waterValue;
    uint32_t timeToSleepSec;
    int wifiTxPower;
    int moistureThreshold;
    int maxSkippedBoots;
};

extern PairedDevice pairedDevices[MAX_DEVICES];
extern size_t deviceCount;

void loadDevicesFromStorage();
void saveDevicesToStorage();
bool addDevice(const uint8_t *newMac);
bool deleteDevice(const uint8_t *mac);
bool renameDevice(const uint8_t *mac, const char *newName);
void saveWateringSettingsToStorage();
void loadWateringSettingsFromStorage();


// Перечисления оставляем — это просто типы данных, они не занимают память
enum WateringMode { MODE_AVERAGE = 0, MODE_MINIMUM = 1 };
enum ValveState { VALVE_IDLE, VALVE_WATERING, VALVE_COOLDOWN };

// Объявляем переменные как extern (память здесь не выделяется)
extern WateringMode currentWateringMode;
extern int globalMoistureThreshold;
extern uint32_t wateringDurationMs;
extern uint32_t cooldownDurationMs;
extern ValveState currentValveState;
extern uint32_t stateTimerMs;
extern uint32_t deviceLastSeenMs[MAX_DEVICES];
extern float deviceLastHumidity[MAX_DEVICES];
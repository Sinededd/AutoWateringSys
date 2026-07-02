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
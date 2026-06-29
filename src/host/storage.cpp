#include "storage.h"
#include <Preferences.h>

Preferences preferences;

PairedDevice pairedDevices[MAX_DEVICES];
size_t deviceCount = 0;

void printMac(const uint8_t *mac) {
    Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void loadDevicesFromStorage() {
    preferences.begin("espnow", true);
    deviceCount = preferences.getUInt("count", 0);

    if (deviceCount > 0) {
        // Читаем массив структур целиком
        preferences.getBytes("macs", pairedDevices, deviceCount * sizeof(PairedDevice));
        Serial.printf("[Storage] Загружено устройств: %d\n", deviceCount);
        for (size_t i = 0; i < deviceCount; i++) {
            Serial.print(" -> ");
            printMac(pairedDevices[i].mac);
            Serial.printf(" (%s)\n", pairedDevices[i].name);
        }
    } else {
        Serial.println("[Storage] Список устройств пуст.");
    }
    preferences.end();
}

void saveDevicesToStorage() {   
    preferences.begin("espnow", false);
    preferences.putUInt("count", deviceCount);
    
    if (deviceCount > 0) {
        preferences.putBytes("macs", pairedDevices, deviceCount * sizeof(PairedDevice));
    } else {
        preferences.remove("macs"); 
    }
    preferences.end();
    Serial.println("[Storage] Изменения сохранены в NVS.");
}

bool addDevice(const uint8_t *newMac) {
    if (deviceCount >= MAX_DEVICES) return false;

    for (size_t i = 0; i < deviceCount; i++) {
        if (memcmp(pairedDevices[i].mac, newMac, 6) == 0) return true;
    }

    memcpy(pairedDevices[deviceCount].mac, newMac, 6);
    // По умолчанию имя — это тот же MAC в виде строки
    snprintf(pairedDevices[deviceCount].name, 32, "%02X:%02X:%02X:%02X:%02X:%02X", 
             newMac[0], newMac[1], newMac[2], newMac[3], newMac[4], newMac[5]);
    
    deviceCount++;
    saveDevicesToStorage();
    return true;
}

bool deleteDevice(const uint8_t *mac) {
    int targetIndex = -1;
    for (size_t i = 0; i < deviceCount; i++) {
        if (memcmp(pairedDevices[i].mac, mac, 6) == 0) {
            targetIndex = i;
            break;
        }
    }
    if (targetIndex == -1) return false;

    for (size_t i = targetIndex; i < deviceCount - 1; i++) {
        pairedDevices[i] = pairedDevices[i + 1];
    }
    deviceCount--;
    memset(&pairedDevices[deviceCount], 0, sizeof(PairedDevice));
    saveDevicesToStorage();
    return true;
}

// Новая функция смены имени
bool renameDevice(const uint8_t *mac, const char *newName) {
    for (size_t i = 0; i < deviceCount; i++) {
        if (memcmp(pairedDevices[i].mac, mac, 6) == 0) {
            strncpy(pairedDevices[i].name, newName, 31);
            pairedDevices[i].name[31] = '\0'; // Гарантируем ноль на конце
            saveDevicesToStorage();
            return true;
        }
    }
    return false;
}
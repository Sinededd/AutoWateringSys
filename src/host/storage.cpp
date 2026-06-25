#include "storage.h"
#include <Preferences.h>

// Создаем объект для работы с NVS внутри этого файла
Preferences preferences;

// Выделяем реальное место в памяти под переменные
uint8_t pairedDevices[MAX_DEVICES][6];
size_t deviceCount = 0;

void printMac(const uint8_t *mac)
{
    Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void loadDevicesFromStorage()
{
    preferences.begin("espnow", true);
    deviceCount = preferences.getUInt("count", 0);

    if (deviceCount > 0)
    {
        preferences.getBytes("macs", pairedDevices, deviceCount * 6);
        Serial.printf("[Storage] Успешно загружено устройств: %d\n", deviceCount);
        for (size_t i = 0; i < deviceCount; i++)
        {
            Serial.print(" -> ");
            printMac(pairedDevices[i]);
            Serial.println();
        }
    }
    else
    {
        Serial.println("[Storage] Список устройств в памяти пуст.");
    }
    preferences.end();
}

void saveDevicesToStorage()
{   
    preferences.begin("espnow", false);
    preferences.putUInt("count", deviceCount);
    
    if (deviceCount > 0) 
    {
        preferences.putBytes("macs", pairedDevices, deviceCount * 6);
    } 
    else 
    {
        // Если удалили последнее устройство, лучше вообще стереть ключ из NVS
        preferences.remove("macs"); 
    }
    
    preferences.end();
    Serial.println("[Storage] Изменения успешно сохранены во флеш-память NVS.");
}

bool addDevice(const uint8_t *newMac)
{
    if (deviceCount >= MAX_DEVICES)
    {
        Serial.println("[Storage] Ошибка: Достигнут лимит устройств!");
        return false;
    }

    for (size_t i = 0; i < deviceCount; i++)
    {
        if (memcmp(pairedDevices[i], newMac, 6) == 0)
        {
            Serial.println("[Storage] Устройство уже известно.");
            return true;
        }
    }

    memcpy(pairedDevices[deviceCount], newMac, 6);
    deviceCount++;

    saveDevicesToStorage();
    return true;
}

bool deleteDevice(const uint8_t *mac)
{
    int targetIndex = -1;

    for (size_t i = 0; i < deviceCount; i++)
    {
        if (memcmp(pairedDevices[i], mac, 6) == 0)
        {
            targetIndex = i;
            break;
        }
    }

    if (targetIndex == -1)
    {
        Serial.println("[Storage] Ошибка: Такого устройства нет в списке.");
        return false;
    }

    for (size_t i = targetIndex; i < deviceCount - 1; i++)
    {
        memcpy(pairedDevices[i], pairedDevices[i + 1], 6);
    }

    deviceCount--;

    memset(pairedDevices[deviceCount], 0, 6);
    Serial.println("[Storage] Устройство успешно удалено из оперативной памяти.");
    saveDevicesToStorage();
    
    return true;
}
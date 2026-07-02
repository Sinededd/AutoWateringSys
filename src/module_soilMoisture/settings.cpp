#include "settings.h"
#include "config.h"
#include <Preferences.h>

// Выделяем память под переменные выполнения
int r_airValue;
int r_waterValue;
uint32_t r_timeToSleepSec;
int r_wifiTxPower;
int r_moistureThreshold;
int r_maxSkippedBoots;

RTC_DATA_ATTR int skippedBootsCounter = 0;

// Вспомогательные функции валидации (видны только в этом файле)
static bool isValidCalibration(int air, int water) {
    // 1. Для емкостного датчика значение в воздухе ВСЕГДА больше, чем в воде.
    // 2. Диапазон должен быть в пределах разумного для ADC (например, от 200мВ до 3600мВ)
    if (air <= water) return false;
    if (air < 500 || air > 3600) return false;
    if (water < 100 || water > 3000) return false;
    return true;
}

static bool isValidSleepInterval(uint32_t seconds) {
    // Защита от бесконечного цикла перезагрузки: сон в 0 секунд превратит плату 
    // в неуправляемый генератор бутлупов, который будет тяжело перепрошить по воздуху.
    // Ограничим минимум 5 секундами, а максимум, например, 30 днями.
    return (seconds >= 5 && seconds <= 2592000);
}

static bool isValidWifiPower(int powerLevel) {
    // В SDK ESP32 перечисление wifi_power_t имеет значения от -4 (WIFI_POWER_MINUS_1dBm)
    // до 78 (WIFI_POWER_19_5dBm) или 84 (WIFI_POWER_21dBm на некоторых ревизиях).
    // Передача значений вне этого диапазона может «уронить» внутренний стек Wi-Fi.
    return (powerLevel >= -4 && powerLevel <= 84);
}

static bool isValidMoistureThreshold(int threshold) {
    // Порог не может быть отрицательным или слишком огромным (ограничим от 0% до 20%)
    return (threshold >= 0 && threshold <= 20);
}

static bool isValidMaxSkippedBoots(int count) {
    return (count >= 0 && count <= 100);
}


void initAndLoadSettings() {
    Preferences prefs;
    prefs.begin("sys_settings", true);

    // Читаем из NVS
    int loadedAir       = prefs.getInt("air_val", DEFAULT_AIR_VALUE);
    int loadedWater     = prefs.getInt("water_val", DEFAULT_WATER_VALUE);
    uint32_t loadedSleep= prefs.getUInt("sleep_sec", DEFAULT_TIME_TO_SLEEP_SEC);
    int loadedWifiPwr   = prefs.getInt("wifi_pwr", DEFAULT_WIFI_POWER);
    int loadedThreshold = prefs.getInt("moi_thresh", DEFAULT_MOISTURE_THRESHOLD);
    int loadedMaxSkipped = prefs.getInt("max_skip", DEFAULT_MAX_SKIPPED_BOOTS);

    prefs.end();

    // Проверяем то, что прочитали. Если в флеш-памяти мусор — берем жесткий дефолт из config.h
    if (isValidCalibration(loadedAir, loadedWater)) {
        r_airValue = loadedAir;
        r_waterValue = loadedWater;
    } else {
        r_airValue = DEFAULT_AIR_VALUE;
        r_waterValue = DEFAULT_WATER_VALUE;
#ifdef DEBUG_ENABLE
        Serial.println("[SYS] Калибровка в NVS повреждена! Сброшено на дефолт.");
#endif
    }

    if (isValidSleepInterval(loadedSleep)) {
        r_timeToSleepSec = loadedSleep;
    } else {
        r_timeToSleepSec = DEFAULT_TIME_TO_SLEEP_SEC;
    }

    if (isValidWifiPower(loadedWifiPwr)) {
        r_wifiTxPower = loadedWifiPwr;
    } else {
        r_wifiTxPower = DEFAULT_WIFI_POWER;
    }

    if (isValidMoistureThreshold(loadedThreshold)) {
        r_moistureThreshold = loadedThreshold;
    } else {
        r_moistureThreshold = DEFAULT_MOISTURE_THRESHOLD;
    }

    if (isValidMaxSkippedBoots(loadedMaxSkipped)) {
        r_maxSkippedBoots = loadedMaxSkipped;
    } else {
        r_maxSkippedBoots = DEFAULT_MAX_SKIPPED_BOOTS;
    }
}

bool saveCalibrationSettings(int air, int water) {
    if (!isValidCalibration(air, water)) {
#ifdef DEBUG_ENABLE
        Serial.printf("[ERROR] Неверные данные калибровки: Air=%d, Water=%d\n", air, water);
#endif
        return false; // Отклоняем запись
    }

    Preferences prefs;
    prefs.begin("sys_settings", false);
    prefs.putInt("air_val", air);
    prefs.putInt("water_val", water);
    prefs.end();
    
    r_airValue = air;
    r_waterValue = water;
    return true;
}

bool saveSleepInterval(uint32_t seconds) {
    if (!isValidSleepInterval(seconds)) {
#ifdef DEBUG_ENABLE
        Serial.printf("[ERROR] Недопустимый интервал сна: %u сек\n", seconds);
#endif
        return false;
    }

    Preferences prefs;
    prefs.begin("sys_settings", false);
    prefs.putUInt("sleep_sec", seconds);
    prefs.end();
    
    r_timeToSleepSec = seconds;
    return true;
}

bool saveWifiPower(int powerLevel) {
    if (!isValidWifiPower(powerLevel)) {
#ifdef DEBUG_ENABLE
        Serial.printf("[ERROR] Неверная мощность WiFi: %d\n", powerLevel);
#endif
        return false;
    }

    Preferences prefs;
    prefs.begin("sys_settings", false);
    prefs.putInt("wifi_pwr", powerLevel);
    prefs.end();
    
    r_wifiTxPower = powerLevel;
    return true;
}

bool saveMoistureThreshold(int threshold) {
    if (!isValidMoistureThreshold(threshold)) return false;
    Preferences prefs;
    prefs.begin("sys_settings", false);
    prefs.putInt("moi_thresh", threshold);
    prefs.end();
    r_moistureThreshold = threshold;
    return true;
}

bool saveMaxSkippedBoots(int count) {
    if (!isValidMaxSkippedBoots(count)) return false;
    
    Preferences prefs;
    prefs.begin("sys_settings", false);
    prefs.putInt("max_skip", count);
    prefs.end();
    
    r_maxSkippedBoots = count; // Обновляем переменную в оперативной памяти
    return true;
}
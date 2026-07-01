#pragma once
#include <Arduino.h>

extern int r_airValue;
extern int r_waterValue;
extern uint32_t r_timeToSleepSec;
extern int r_wifiTxPower;

void initAndLoadSettings();

// Изменили void на bool
bool saveCalibrationSettings(int air, int water);
bool saveSleepInterval(uint32_t seconds);
bool saveWifiPower(int powerLevel);
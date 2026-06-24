#pragma once

#include <Arduino.h>

#define MAX_DEVICES 20 


extern uint8_t pairedDevices[MAX_DEVICES][6];
extern size_t deviceCount;


void loadDevicesFromStorage();
void saveDevicesToStorage();
bool addDevice(const uint8_t *newMac);
void printMac(const uint8_t *mac);
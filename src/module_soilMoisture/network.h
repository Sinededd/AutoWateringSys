#pragma once

#include "../common/types.h"

// Экспортируем глобальные RTC и системные переменные, чтобы main.cpp их видел
extern PairingStatus pairingStatus;
extern uint8_t hostMac[6];
extern unsigned int globalReadingId;
extern volatile bool txDone;
extern bool updateMAC;
extern float lastSentMoisture;

// Интерфейсные функции для работы с сетью
void initWiFiAndEspNow();
void sendPairingRequest();
void sendSensorData(float hum);
void resetNetworkSettings();
void sendSettingsReport();

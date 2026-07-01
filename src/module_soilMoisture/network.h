#pragma once

#include "types.h"

// Экспортируем глобальные RTC и системные переменные, чтобы main.cpp их видел
extern RTC_DATA_ATTR PairingStatus pairingStatus;
extern RTC_DATA_ATTR uint8_t hostMac[6];
extern RTC_DATA_ATTR unsigned int globalReadingId;
extern volatile bool txDone;
extern bool updateMAC;

// Интерфейсные функции для работы с сетью
void initWiFiAndEspNow();
void sendPairingRequest();
void sendSensorData(float hum);
void resetNetworkSettings();

#include "sensor.h"
#include "config.h"
#include <Arduino.h>

float readSoilMoisture() {
    digitalWrite(SENSOR_POWER_PIN, HIGH);
    delay(350); 
    long analogMilliVolts = 0;
    for (int i = 0; i < 5; i++) {
        analogMilliVolts += analogReadMilliVolts(SENSOR_ANALOG_PIN); 
        delay(5);
    }
    analogMilliVolts /= 5; 
    digitalWrite(SENSOR_POWER_PIN, LOW);
    DEBUG_PRINTF("[SENSOR] Напряжение на пине: %d мВ\n", analogMilliVolts);

    int moisturePercent = map(analogMilliVolts, AIR_VALUE, WATER_VALUE, 0, 100);
    moisturePercent = constrain(moisturePercent, 0, 100);
    return (float)moisturePercent;
}
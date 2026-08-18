#include "sensor.h"
#include "config.h"
#include "settings.h"
#include <Arduino.h>
#include "driver/gpio.h"

float readSoilMoisture() {
    pinMode(SENSOR_POWER_PIN, OUTPUT);
    pinMode(SENSOR_ANALOG_PIN, INPUT);

    digitalWrite(SENSOR_POWER_PIN, HIGH);
    delay(350); 
    long analogMilliVolts = 0;
    for (int i = 0; i < 5; i++) {
        analogMilliVolts += analogReadMilliVolts(SENSOR_ANALOG_PIN); 
        delay(5);
    }
    analogMilliVolts /= 5; 
    digitalWrite(SENSOR_POWER_PIN, LOW);
    gpio_reset_pin((gpio_num_t)SENSOR_POWER_PIN);
    gpio_reset_pin((gpio_num_t)SENSOR_ANALOG_PIN);
    DEBUG_PRINTF("[SENSOR] Напряжение на пине: %d мВ\n", analogMilliVolts);

    int moisturePercent = map(analogMilliVolts, r_airValue, r_waterValue, 0, 100);
    moisturePercent = constrain(moisturePercent, 0, 100);
    return (float)moisturePercent;
}


bool enableSoilMoisture = false;
// Start soil moisture sensor and return value
// This method always keep sensor up, to close it call stopSoilMoisture function
int startSoilMoisture() {
    if(!enableSoilMoisture) {
        pinMode(SENSOR_POWER_PIN, OUTPUT);
        pinMode(SENSOR_ANALOG_PIN, INPUT);
        digitalWrite(SENSOR_POWER_PIN, HIGH);
        enableSoilMoisture = true;
    delay(350); 
    }

    return analogReadMilliVolts(SENSOR_ANALOG_PIN);
}

// Stop working soil moisture sensor
void stopSoilMoisture() {
    if(enableSoilMoisture) {
        digitalWrite(SENSOR_POWER_PIN, LOW);
        gpio_reset_pin((gpio_num_t)SENSOR_POWER_PIN);
        gpio_reset_pin((gpio_num_t)SENSOR_ANALOG_PIN);
        enableSoilMoisture = false;
    }
}
#include "Valve.h"
#include "Logger.h"

static const char* TAG = "VALVE";

Valve::Valve(uint8_t pin, bool activeLow, unsigned long maxDurationMs)
    : pin(pin), activeLow(activeLow), maxDurationMs(maxDurationMs) {}

void Valve::begin() {
    pinMode(pin, OUTPUT);
    close();
    LOG_D(TAG, "Valve initialized on pin %d", pin);
}

void Valve::open() {
    if (active) return;
    digitalWrite(pin, activeLow ? LOW : HIGH);
    active = true;
    openedAtMs = millis();
    LOG_I(TAG, "Valve on pin %d OPENED", pin);
}

void Valve::close() {
    digitalWrite(pin, activeLow ? HIGH : LOW);
    active = false;
    LOG_I(TAG, "Valve on pin %d CLOSED", pin);
}

void Valve::update() {
    if (active && (millis() - openedAtMs >= maxDurationMs)) {
        LOG_E(TAG, "Valve on pin %d FORCE CLOSED by safety timeout!", pin);
        close();
    }
}
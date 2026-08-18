#include "Led.h"

Led::Led(uint8_t pin, bool activeLow) : pin(pin), activeLow(activeLow) {}

void Led::begin() {
    pinMode(pin, OUTPUT);
    turnOff();
}

void Led::setRawState(bool state) {
    active = state;
    digitalWrite(pin, activeLow ? !active : active);
}

void Led::turnOn() {
    blinkCount = 0;
    setRawState(true);
}

void Led::turnOff() {
    blinkCount = 0;
    setRawState(false);
}

void Led::toggle() {
    setRawState(!active);
}

void Led::slowBlinking(int times) {
    isSlowBlinking = true;
    blinkCount = times * 2;
    lastBlinkTime = millis() - 1000;
}

void Led::fastBlinking(int times) {
    isSlowBlinking = false;
    blinkCount = times * 2;
    lastBlinkTime = millis() - 200;
}

void Led::update() {
    if (blinkCount > 0) {
        unsigned long currentTime = millis();
        uint32_t interval = isSlowBlinking ? 1000 : 200;

        if (currentTime - lastBlinkTime >= interval) {
            lastBlinkTime = currentTime;
            toggle();
            blinkCount--;
        }
    }
}
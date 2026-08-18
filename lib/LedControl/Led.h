#pragma once
#include <Arduino.h>

class Led {
private:
    uint8_t pin;
    bool active = false;
    bool activeLow;
    bool isSlowBlinking = false;
    int blinkCount = 0;
    unsigned long lastBlinkTime = 0;

    /// @brief Sets state of the LED directly without affecting the blinking state.
    void setRawState(bool state);
public:
    Led(uint8_t pin, bool activeLow = false);

    /// @brief Initializes the built-in LED by setting the pin mode and turning it off.
    void begin();

    /// @brief Needs to ability blinking the LED. Called in the main loop.
    void update();

    void turnOn();

    void turnOff();

    void toggle();

    bool isActive() const { return active; }

    void slowBlinking(int times = 5);

    void fastBlinking(int times = 5);
};

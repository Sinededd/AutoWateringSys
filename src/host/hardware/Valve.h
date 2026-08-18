#pragma once
#include <Arduino.h>

#include "Config.h"



class Valve {
private:
    uint8_t pin;
    bool active = false;
    bool activeLow;
    unsigned long openedAtMs = 0;
    unsigned long maxDurationMs;

public:
    /// @brief Constructs a new Valve instance.
    /// @param pin The pin number to which the valve is connected.
    /// @param activeLow Whether the valve is active low (true) or active high (false).
    /// @param maxDurationMs The maximum duration (in milliseconds) for which the valve can remain open.
    Valve(uint8_t pin, bool activeLow = false, unsigned long maxDurationMs = Config::Defaults::VALVE_MAX_WATERING_DURATION_MS);

    /// @brief Initializes the valve by setting the pin mode and closing it.
    /// @note This should be called in the setup() function.
    void begin();

    void open();
    void close();

    /// @brief Updates the valve state, checking if it should be automatically closed due to exceeding the maximum duration.
    ///
    /// Default duration is 5 minutes (300000 ms).
    /// @note This should be called in the loop() function.
    void update();

    bool isOpen() const { return active; }
};
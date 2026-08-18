#pragma once
#include <Arduino.h>


namespace Config {

    // --- Purpose of pins GPIO ---
    namespace Pins {
        constexpr uint8_t LED_PIN = LED_BUILTIN;   // built-in LED
        constexpr uint8_t VALVE_PIN = 4; // water pump relay
    }

    // --- Hardware settings ---
    namespace Hardware {
        constexpr unsigned long SERIAL_BAUD_RATE = 115200;
    }

    // --- Default settings and limits ---
    namespace Defaults {
        constexpr unsigned long VALVE_MAX_WATERING_DURATION_MS = 300000; // 5 minutes
    }
}
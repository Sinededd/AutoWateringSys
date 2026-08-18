#pragma once
#include <Arduino.h>


namespace Config {

    // --- Purpose of pins GPIO ---
    namespace Pins {
        constexpr uint8_t LED_PIN = 2;   // built-in LED
        constexpr uint8_t VALVE_PIN = 4; // water pump relay
    }

    // --- Hardware settings ---
    namespace Hardware {
        constexpr unsigned long SERIAL_BAUD_RATE = 115200;
    }

    // --- Default settings and limits ---
    namespace Defaults {

    }
}
#pragma once

#include <Arduino.h>
#include <Led.h>
#include "../hardware/Valve.h"


class Led;
class Valve;

class CliManager {
private:
    Led* led = nullptr;
    Valve* valve = nullptr;

    char buffer[64];
    uint8_t pos = 0;

    struct Command {
        const char* name;
        void (CliManager::*handler)(const char* arg1, const char* arg2);
        const char* help;
    };

    static const Command commands[];
    static const size_t numCommands;

    void cmdLed(const char* arg1, const char* arg2);
    void cmdValve(const char* arg1, const char* arg2);
    void cmdHelp(const char* arg1, const char* arg2);

    void executeCommand(char* inputBuffer);

public:
    CliManager() = default;

    void setLed(Led* ledPtr) { led = ledPtr; }
    void setValve(Valve* valvePtr) { valve = valvePtr; }

    void update();
};
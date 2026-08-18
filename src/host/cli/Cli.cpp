#include "Cli.h"



const CliManager::Command CliManager::commands[] = {
    {"led",   &CliManager::cmdLed,   "led <on|off|slow|fast> [count] — LED control"},
    {"valve", &CliManager::cmdValve, "valve [open|close]             — relay control"},
    {"help",  &CliManager::cmdHelp,  "help                           — display all commands"}
};

const size_t CliManager::numCommands = sizeof(commands) / sizeof(commands[0]);

void CliManager::cmdLed(const char *arg1, const char *arg2) {
    if (led == nullptr) {
        Serial.println(F("Error: LED device is not attached/configured!"));
        return;
    }

    if (!arg1) {
        Serial.println(F("Error: argument required (on, off, slow, fast)"));
        return;
    }

    if (strcmp(arg1, "on") == 0) {
        led->turnOn();
    }
    else if (strcmp(arg1, "off") == 0) {
        led->turnOff();
    }
    else if (strcmp(arg1, "slow") == 0) {
        int count = arg2 ? atoi(arg2) : 5;
        led->slowBlinking(count);
    }
    else if (strcmp(arg1, "fast") == 0) {
        int count = arg2 ? atoi(arg2) : 5;
        led->fastBlinking(count);
    }
    else {
        Serial.printf("Unknown LED parameter: %s\n", arg1);
    }
}

void CliManager::cmdValve(const char *arg1, const char *arg2) {
    if (valve == nullptr) {
        Serial.println(F("Error: Valve device is not attached/configured!"));
        return;
    }

    if (!arg1) {
        Serial.printf(F("Current state of valve: %s\n"), valve->isOpen() ? "opened" : "closed");
        return;
    }

    if(strcmp(arg1, "open") == 0) {
        valve->open();
    }
    else if(strcmp(arg1, "close") == 0) {
        valve->close();
    }
    else {
        Serial.printf("Unknown Valve parameter: %s\n", arg1);
    }
}

void CliManager::cmdHelp(const char *arg1, const char *arg2) {
    Serial.println(F("\n=== Available Commands ==="));
    for (size_t i = 0; i < numCommands; i++) {
        Serial.printf(" - %s\n", commands[i].help);
    }
    Serial.println();
}

void CliManager::executeCommand(char *inputBuffer) {
    char *cmd = strtok(inputBuffer, " ");
    if (!cmd) return;

    char *arg1 = strtok(NULL, " ");
    char *arg2 = strtok(NULL, " ");

    for (size_t i = 0; i < numCommands; i++) {
        if (strcmp(cmd, commands[i].name) == 0) {
            (this->*commands[i].handler)(arg1, arg2);
            return;
        }
    }

    Serial.printf("Unknown command '%s'. Type 'help' for a list of commands.\n", cmd);
}

void CliManager::update() {
    while (Serial.available() > 0) {
        char c = Serial.read();

        if (c == '\r' || c == '\n') {
            if (pos > 0) {
                buffer[pos] = '\0';
                executeCommand(buffer);
                pos = 0;
            }
        }
        else if (pos < sizeof(buffer) - 1) {
            buffer[pos++] = c;
        }
    }
}
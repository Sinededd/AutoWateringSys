#pragma once

#include <Arduino.h>

enum MessageType { PAIRING, DATA };
enum PairingStatus { NOT_PAIRED, PAIRING_REQUESTED, PAIRED };

typedef struct struct_message {
    uint8_t msgType;
    uint8_t macAddr[6];
    float hum;
    unsigned int readingId;
} struct_message;

typedef struct struct_pairing {
    uint8_t msgType;
    uint8_t isReply; 
    uint8_t macAddr[6];
} struct_pairing;


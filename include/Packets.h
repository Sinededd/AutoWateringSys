#pragma once
#include <Arduino.h>


enum class PacketType : uint8_t {
    UNKNOWN = 0,
    HELLO
};

struct __attribute__((packed)) PacketHeader {
    PacketType type;
    uint8_t senderId;
};

struct __attribute__((packed)) HelloPacket {
    PacketHeader header;
    uint32_t timestamp;

    HelloPacket(uint8_t senderId, uint32_t ts) {
        header.type = PacketType::HELLO;
        header.senderId = senderId;
        timestamp = ts;
    }
};
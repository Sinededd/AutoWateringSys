#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include <map>
#include <Packets.h>
#include <WiFiType.h>
#include <Converter.h>
#include <Logger.h>
#include <WiFiGeneric.h>

using RawPacketHandler = std::function<void(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int len)>;

class EspNowManager
{
private:
    static EspNowManager *instance;
    std::map<PacketType, RawPacketHandler> handlers;

    wifi_mode_t wifiMode;
    wifi_power_t wifiPower;

    static void OnDataSendStatic(const esp_now_send_info_t *tx_info, esp_now_send_status_t status);
    static void OnDataRecvStatic(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int len);

    /// @brief Internal function to handle received data.
    void handleReceive(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int len);

public:
    EspNowManager(wifi_mode_t wifi_mode = WIFI_STA, wifi_power_t wifi_power = WIFI_POWER_19_5dBm);

    bool begin();

    wifi_power_t getWifiPower() { return wifiPower; }
    void setWifiPower(wifi_power_t wifi_power);

    bool addPeer(const uint8_t *macAddress, uint8_t channel = 0);

    /// @brief Register a handler for a specific packet structure
    template <typename PacketStruct>
    void onPacket(PacketType type, std::function<void(const esp_now_recv_info_t *esp_now_info, const PacketStruct &packet)> cb)
    {
        LOG_D("ESP_NOW_MANAGER", "Register handler for packet type: %d, size: %zu bytes", static_cast<int>(type), sizeof(PacketStruct));
        handlers[type] = [cb](const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int len)
        {
            if (len == sizeof(PacketStruct))
            {
                const PacketStruct *packet = reinterpret_cast<const PacketStruct *>(data);
                cb(esp_now_info, *packet);
            }
            else
            {
                LOG_W("ESP_NOW_MANAGER", "Packet size mismatch! Expected: %zu, Received: %d", sizeof(PacketStruct), len);
            }
        };
    }

    /// @brief Send any packet structure
    template <typename T>
    bool send(const uint8_t *macAddress, const T &packet)
    {
        static_assert(sizeof(T) <= 250, "Packet size exceeds ESP-NOW limit of 250 bytes!");
        LOG_D("ESP_NOW_MANAGER", "Sending packet to MAC: %s, size: %zu bytes",
              macToString(macAddress).c_str(),
              sizeof(T));
        return esp_now_send(macAddress, (const uint8_t *)&packet, sizeof(T)) == ESP_OK;
    }
};

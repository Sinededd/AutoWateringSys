#pragma once

#include <Arduino.h>
#include <string>

/// @brief Convert to string (AA:BB:CC:DD:EE:FF)
inline std::string macToString(const uint8_t *mac)
{
    char buffer[18];
    snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(buffer);
}

/// @brief Convert from string (AA:BB:CC:DD:EE:FF) to uint8_t
inline bool stringToMac(const std::string &str, uint8_t *mac)
{
    if (str.length() != 17)
        return false;

    int values[6];
    if (sscanf(str.c_str(), "%02X:%02X:%02X:%02X:%02X:%02X",
               &values[0], &values[1], &values[2],
               &values[3], &values[4], &values[5]) != 6)
    {
        return false;
    }

    for (int i = 0; i < 6; i++)
    {
        mac[i] = static_cast<uint8_t>(values[i]);
    }
    return true;
}

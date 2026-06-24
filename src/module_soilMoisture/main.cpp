#define DEBUG_ENABLE

#ifdef DEBUG_ENABLE
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTF(...)
#endif


#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

enum MessageType
{
    PAIRING,
    DATA,
};

typedef struct struct_message
{
    uint8_t msgType;
    uint8_t macAddr[6];
    float hum;
    unsigned int readingId;
} struct_message;

typedef struct struct_pairing
{
    uint8_t msgType;
    uint8_t isReply;   // 0 - запрос от датчика, 1 - ответ от хоста
    uint8_t macAddr[6];
} struct_pairing;

struct_message myData;  // data to send
struct_message inData;  // data received
struct_pairing pairingData;

uint8_t hostMac[6];
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

enum PairingStatus { NOT_PAIRED, PAIRING_REQUESTED, PAIRED };
PairingStatus pairingStatus = NOT_PAIRED;

unsigned long lastTransmission = 0;
const unsigned long sendInterval = 5000;
const unsigned long pairingInterval = 3000;

void OnDataSent(const wifi_tx_info_t *mac_addr, esp_now_send_status_t status)
{
    Serial.print("Status доставки пакета: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Успешно" : "Ошибка");
}

void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len)
{
    if (len == 0) return;

    uint8_t type = incomingData[0];

    if (type == PAIRING)
    {
        memcpy(&pairingData, incomingData, sizeof(pairingData));
        
        if (pairingData.isReply != 1) {
            DEBUG_PRINTLN("Поймали чужой запрос сопряжения в эфире. Игнорируем.");
            return; 
        }
        
        // Если мы здесь — это точно ответил ХОСТ
        DEBUG_PRINTLN("Сопряжение успешно! MAC-адрес Хоста: ");
        for (int i = 0; i < 6; i++) {
            hostMac[i] = pairingData.macAddr[i];
            DEBUG_PRINTF("%02X:", hostMac[i]);
        }
        DEBUG_PRINTLN();

        // Регистрируем Хост в пиры... (дальше твой код без изменений)
        esp_now_peer_info_t peerInfo;
        memset(&peerInfo, 0, sizeof(peerInfo));
        memcpy(peerInfo.peer_addr, hostMac, 6);
        peerInfo.channel = 0; 
        peerInfo.encrypt = false;

        if (esp_now_is_peer_exist(hostMac)) {
            esp_now_del_peer(hostMac);
        }

        if (esp_now_add_peer(&peerInfo) == ESP_OK) {
            Serial.println("Хост успешно добавлен в список пиров.");
            pairingStatus = PAIRED;
        }
    }
}

void setup()
{
    #ifdef DEBUG_ENABLE
    Serial.begin(115200);
    delay(1000);
    #endif

    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    Serial.print("Мой MAC-адрес: ");
    Serial.println(WiFi.macAddress());

    if (esp_now_init() != ESP_OK) {
        Serial.println("Ошибка инициализации ESP-NOW");
        return;
    }

    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);

    // Добавляем широковещательный адрес для первичного сопряжения
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Не удалось добавить широковещательный адрес.");
    }
}

void loop()
{
    unsigned long currentMillis = millis();

    switch (pairingStatus)
    {
        case NOT_PAIRED:
        {
            if (currentMillis - lastTransmission >= pairingInterval)
            {
                lastTransmission = currentMillis;

                pairingData.msgType = PAIRING;
                pairingData.isReply = 0;
                WiFi.macAddress(pairingData.macAddr); // Записываем СВОЙ мак, чтобы Хост нас узнал

                DEBUG_PRINTLN("Отправка запроса на сопряжение (Broadcast)...");
                esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&pairingData, sizeof(pairingData));
                
                if (result == ESP_OK) {
                    pairingStatus = PAIRING_REQUESTED;
                }
            }
            break;
        }

        case PAIRING_REQUESTED:
        {
            // Ждем ответа. Если за 3 секунды Хост не ответил, сбрасываем статус и пробуем снова
            if (currentMillis - lastTransmission >= pairingInterval) {
                Serial.println("Превышено время ожидания ответа. Повтор...");
                pairingStatus = NOT_PAIRED;
            }
            break;
        }

        case PAIRED:
        {
            if (currentMillis - lastTransmission >= sendInterval)
            {
                lastTransmission = currentMillis;

                myData.msgType = DATA;
                WiFi.macAddress(myData.macAddr); // Пакет данных тоже маркируем своим MAC
                myData.hum = random(4000, 8000) / 100.0; // Эмуляция случайной влажности (40.00% - 80.00%)
                myData.readingId++;

                Serial.printf("Отправка данных на Хост. Влажность: %.2f%%, Пакет №: %u\n", myData.hum, myData.readingId);
                
                // Отправляем целенаправленно на сохраненный MAC Хоста
                esp_now_send(hostMac, (uint8_t *)&myData, sizeof(myData));
            }
            break;
        }
    }
}
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
#include <esp_wifi.h>
#include <Preferences.h>
#include <Button2.h>

// === КНОПКА: Настройки ===
const int BUTTON_PIN = 3;
Button2 btn;

// === LED indicator params ===
bool ledState = true;
bool curentState = true;
unsigned long lastToggle = 0;
bool updateMAC = 0; //  fag is set when set new MAC for 2.5 seconds

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
    uint8_t isReply; // 0 - запрос от датчика, 1 - ответ от хоста
    uint8_t macAddr[6];
} struct_pairing;

struct_message myData; // data to send
struct_message inData; // data received
struct_pairing pairingData;

uint8_t hostMac[6];
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

enum PairingStatus
{
    NOT_PAIRED,
    PAIRING_REQUESTED,
    PAIRED
};
PairingStatus pairingStatus = NOT_PAIRED;

unsigned long lastTransmission = 0;
const unsigned long sendInterval = 5000;
const unsigned long pairingInterval = 3000;
unsigned long pairedTime = 0;

void OnDataSent(const wifi_tx_info_t *mac_addr, esp_now_send_status_t status)
{
    Serial.print("Status доставки пакета: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Успешно" : "Ошибка");
}

void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len)
{
    if (len == 0)
        return;

    uint8_t type = incomingData[0];

    if (type == PAIRING)
    {
        memcpy(&pairingData, incomingData, sizeof(pairingData));

        if (pairingData.isReply != 1)
        {
            DEBUG_PRINTLN("Поймали чужой запрос сопряжения в эфире. Игнорируем.");
            return;
        }

        // Если мы здесь — это точно ответил ХОСТ
        DEBUG_PRINTLN("Сопряжение успешно! MAC-адрес Хоста: ");
        for (int i = 0; i < 6; i++)
        {
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

        if (esp_now_is_peer_exist(hostMac))
        {
            esp_now_del_peer(hostMac);
        }

        if (esp_now_add_peer(&peerInfo) == ESP_OK)
        {
            Serial.println("Хост успешно добавлен в список пиров.");
            pairingStatus = PAIRED;
            pairedTime = millis();
        }
    }
}

unsigned long rapidBlinkTimer = 0;
void updateLedIndicator()
{
    unsigned long currentMillis = millis();

    if (updateMAC && currentMillis - lastToggle < 2500)
    {
        if (currentMillis - rapidBlinkTimer > 175)
        {
            rapidBlinkTimer = currentMillis;
            ledState = !ledState;
            digitalWrite(8, ledState);
        }
        return;
    }
    else
    {
        updateMAC = 0;
    }

    switch (pairingStatus)
    {
    case NOT_PAIRED:
    case PAIRING_REQUESTED:
        // В обоих режимах поиска мигаем раз в секунду (500 мс вкл / 500 мс выкл)
        if (currentMillis - lastToggle >= 1000)
        {
            ledState = !ledState;
            lastToggle = currentMillis;
        }
        break;

    case PAIRED:
        if (currentMillis - pairedTime > 15000)
        {
            ledState = true;
        }
        else
        {
            ledState = false;
        }
        break;
    }

    if (curentState != ledState)
    {
        curentState = ledState;
        digitalWrite(8, curentState);
    }
}

void loadMacAddress()
{
    uint8_t macBuf[6];
    Preferences preferences;

    preferences.begin("wifi_cfg", false);
    size_t macLength = preferences.getBytes("stored_mac", macBuf, 6);

    if (macLength == 6)
    {
        DEBUG_PRINTLN("Считали ранее сохраненный кастомный MAC из NVS.");
    }
    else
    {
        DEBUG_PRINTLN("Кастомный MAC не найден. Генерируем новый...");

        randomSeed(analogRead(0) + esp_random());
        for (int i = 0; i < 6; i++)
        {
            macBuf[i] = random(0, 256);
        }
        macBuf[0] = (macBuf[0] & 0xFE) | 0x02; // Локальный unicast

        preferences.putBytes("stored_mac", macBuf, 6);
        DEBUG_PRINTLN("Новый MAC успешно зафиксирован в NVS.");
    }
    preferences.end();

    // Применяем адрес
    esp_err_t result = esp_wifi_set_mac(WIFI_IF_STA, macBuf);

    if (result == ESP_OK)
    {
        DEBUG_PRINT("Текущий рабочий MAC-адрес устройства: ");
        DEBUG_PRINTLN(WiFi.macAddress()); // Теперь мы видим, что применилось!
    }
    else
    {
        DEBUG_PRINTF("Ошибка применения MAC! Код: %d\n", result);
    }
}

void setNewMacAddress()
{
    DEBUG_PRINTLN("\n[RESET] >>> Запуск принудительного сброса всех сетевых настроек! <<<");

    // 1. ОТКЛЮЧАЕМ ESP-NOW СОЕДИНЕНИЕ
    // Если мы были сопряжены, нужно физически удалить хост из памяти ESP-NOW
    if (pairingStatus == PAIRED || pairingStatus == PAIRING_REQUESTED)
    {
        esp_err_t delResult = esp_now_del_peer(hostMac);
        if (delResult == ESP_OK)
        {
            DEBUG_PRINTLN("[ESP-NOW] Старый Хост успешно удален из списка пиров.");
        }
        else if (delResult == ESP_ERR_ESPNOW_NOT_FOUND)
        {
            DEBUG_PRINTLN("[ESP-NOW] Предупреждение: Хост не был найден в списке пиров.");
        }
        else
        {
            DEBUG_PRINTF("[ESP-NOW] Ошибка удаления пира! Код: 0x%X\n", delResult);
        }
    }

    // 2. СБРАСЫВАЕМ ДАННЫЕ О СЕРВЕРЕ И СТАТУСЫ
    pairingStatus = NOT_PAIRED; // Возвращаем логику в loop() к началу поиска
    memset(hostMac, 0, 6);      // Полностью обнуляем старый MAC-адрес хоста в памяти
    lastTransmission = 0;       // Сбрасываем таймер, чтобы новый поиск начался мгновенно

    // 3. ГЕНЕРИРУЕМ НОВЫЙ СЛУЧАЙНЫЙ MAC (Твой базовый код)
    uint8_t randomMac[6];
    randomSeed(analogRead(0) + esp_random());
    for (int i = 0; i < 6; i++)
    {
        randomMac[i] = random(0, 256);
    }
    randomMac[0] = (randomMac[0] & 0xFE) | 0x02; // Локальный unicast

    esp_err_t result = esp_wifi_set_mac(WIFI_IF_STA, randomMac);

    if (result == ESP_OK)
    {
        DEBUG_PRINT("[WIFI] Успешно установлен новый случайный MAC: ");
        DEBUG_PRINTLN(WiFi.macAddress());
    }
    else
    {
        DEBUG_PRINTF("[WIFI] КРИТИЧЕСКАЯ ОШИБКА смены MAC! Код: %d\n", result);
    }

    // 4. ОЧИЩАЕМ ФЛЕШ-ПАМЯТЬ (NVS Preferences)
    Preferences preferences;
    preferences.begin("wifi_cfg", false);

    // Записываем новый сгенерированный MAC устройства
    preferences.putBytes("stored_mac", randomMac, 6);

    // НА БУДУЩЕЕ: Если ты решишь сохранять MAC-адрес сопряженного хоста во флеш,
    // эта строчка принудительно сотрет его оттуда при сбросе:
    if (preferences.isKey("stored_host_mac"))
    {
        preferences.remove("stored_host_mac");
        DEBUG_PRINTLN("[NVS] Сохраненный MAC хоста удален из флеш-памяти.");
    }

    preferences.end();

    DEBUG_PRINTLN("[RESET] Сетевые настройки успешно очищены. Устройство перезапущено в режим поиска.");
}

void handleLongPressDetected(Button2 &b)
{
    Serial.println("[BUTTON] Длинное нажатие зафиксировано (кнопка еще НАЖАТА)!");
    setNewMacAddress();
    updateMAC = 1;
    lastToggle = millis();
}

void setup()
{
#ifdef DEBUG_ENABLE
    Serial.begin(115200);
    delay(1000);
#endif

    btn.begin(BUTTON_PIN);
    btn.setLongClickTime(5000);
    btn.setLongClickDetectedHandler(handleLongPressDetected);

    pinMode(8, OUTPUT);

    WiFi.mode(WIFI_STA);
    loadMacAddress();
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    Serial.print("Мой MAC-адрес: ");
    Serial.println(WiFi.macAddress());

    if (esp_now_init() != ESP_OK)
    {
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

    if (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
        Serial.println("Не удалось добавить широковещательный адрес.");
    }
}

void loop()
{
    btn.loop();

    updateLedIndicator();

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

            if (result == ESP_OK)
            {
                pairingStatus = PAIRING_REQUESTED;
            }
        }
        break;
    }

    case PAIRING_REQUESTED:
    {
        // Ждем ответа. Если за 3 секунды Хост не ответил, сбрасываем статус и пробуем снова
        if (currentMillis - lastTransmission >= pairingInterval)
        {
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
            WiFi.macAddress(myData.macAddr);         // Пакет данных тоже маркируем своим MAC
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
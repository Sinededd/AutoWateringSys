#include <Arduino.h>
#include <Button2.h>
#include "config.h"
#include "types.h"
#include "sensor.h"
#include "network.h"
#include "settings.h"

// Глобальные объекты и переменные времени выполнения (обнуляются при пробуждении)
Button2 btn;
unsigned long lastTransmission = 0;
unsigned long lastToggle = 0;
unsigned long rapidBlinkTimer = 0;

bool ledState = true;
bool currentState = true;
bool dataSentThisBoot = false;
bool forcedAwake = false;

// Индикация состояний светодиодом
void updateLedIndicator()
{

    unsigned long currentMillis = millis();

    if (updateMAC && (currentMillis - lastToggle < RESET_BLINK_TIME))
    {
        if (currentMillis - rapidBlinkTimer > 175)
        {
            rapidBlinkTimer = currentMillis;
            ledState = !ledState;
            digitalWrite(LED_PIN, ledState);
        }
        return;
    }
    else
    {
        if (updateMAC)
        {
            updateMAC = false;
            lastToggle = currentMillis;
        }
    }

    switch (pairingStatus)
    {
    case NOT_PAIRED:
    case PAIRING_REQUESTED:
        if (currentMillis - lastToggle >= 500)
        {
            ledState = !ledState;
            lastToggle = currentMillis;
        }
        break;

    case PAIRED:
        ledState = false; // Выключаем светодиод в режиме сна / бодрствования для экономии батареи
        break;
    }

    if (currentState != ledState)
    {
        currentState = ledState;
        digitalWrite(LED_PIN, currentState);
    }
}

// Обработчики прерываний кнопки
void handleButtonTap(Button2 &b)
{
    Serial.println("Клик кнопки в активном режиме!");
}

void handleLongPressDetected(Button2 &b)
{
    Serial.println("[BUTTON] Удержание 5 сек! Сброс сопряжения...");
    resetNetworkSettings();
    updateMAC = true;
    lastToggle = millis();
}

void setup()
{
    initAndLoadSettings();

#ifdef DEBUG_ENABLE
    Serial.begin(115200);
    delay(50);
    Serial.printf("[SYSTEM] Настройки загружены. Сон: %d сек. Калибровка: Аир=%d, Ватер=%d\n",
                  r_timeToSleepSec, r_airValue, r_waterValue);
#endif

    // Настройка базовой периферии
    pinMode(LED_PIN, OUTPUT);
    pinMode(SENSOR_POWER_PIN, OUTPUT);
    digitalWrite(SENSOR_POWER_PIN, LOW);

    // Настраиваем кнопку и СРАЗУ снимаем фиксацию пина (если она осталась с прошлого сна)
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    gpio_hold_dis((gpio_num_t)BUTTON_PIN);

    // Определение причины просыпания чипа
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO)
    {
        Serial.println("[WAKEUP] Пробуждение по нажатию КНОПКИ.");
        forcedAwake = true;
    }
    else if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER)
    {
        Serial.println("[WAKEUP] Пробуждение по ТАЙМЕРУ.");
        forcedAwake = false;
    }
    else
    {
        Serial.println("[WAKEUP] Первое включение питания или аппаратный сброс.");
        forcedAwake = true;
    }

    // Инициализация менеджера кнопки
    btn.begin(BUTTON_PIN);
    btn.setLongClickTime(5000);
    btn.setTapHandler(handleButtonTap);
    btn.setLongClickDetectedHandler(handleLongPressDetected);

    // Настройка АЦП
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);

    // Конфигурация источников пробуждения из Deep Sleep
    esp_sleep_enable_timer_wakeup(r_timeToSleepSec * 1000000ULL);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
}

void loop()
{
    btn.loop();
    updateLedIndicator();
    unsigned long currentMillis = millis();

    // Режим А: Нет связи — удерживаем плату активной для сопряжения
    if (pairingStatus == NOT_PAIRED)
    {
        if (currentMillis - lastTransmission >= PAIRING_INTERVAL)
        {
            lastTransmission = currentMillis;

            // ИСПРАВЛЕНИЕ 1: Инициализируем Wi-Fi один раз перед отправкой запроса сопряжения
            static bool wifiInitedForPairing = false;
            if (!wifiInitedForPairing)
            {
                initWiFiAndEspNow();
                wifiInitedForPairing = true;
            }

            sendPairingRequest();
        }
        return;
    }

    if (pairingStatus == PAIRING_REQUESTED)
    {
        if (currentMillis - lastTransmission >= PAIRING_INTERVAL)
        {
            Serial.println("Таймаут сопряжения. Повтор...");
            pairingStatus = NOT_PAIRED;
        }
        return;
    }

    // Режим Б: Плата сопряжена (PAIRED)
    if (pairingStatus == PAIRED)
    {

        // Отправляем данные один раз за это пробуждение
        if (!dataSentThisBoot)
        {
            float moisture = readSoilMoisture(); // Считываем датчик
            float diff = std::fabs(moisture - lastSentMoisture);

            // ИЗМЕНЕННОЕ УСЛОВИЕ: порог ИЛИ превышение лимита пропущенных циклов
            if (forcedAwake || lastSentMoisture < 0 || diff >= r_moistureThreshold || skippedBootsCounter >= r_maxSkippedBoots)
            {

#ifdef DEBUG_ENABLE
                if (skippedBootsCounter >= r_maxSkippedBoots)
                {
                    Serial.printf("[SYS] Данные стабильны, но сработал пульс Heartbeat (%d циклов пропущено). Отправка...\n", skippedBootsCounter);
                }
                else
                {
                    Serial.printf("[SYS] Изменение существенно (Было: %.1f%%, Стало: %.1f%%). Отправка...\n", lastSentMoisture, moisture);
                }
#endif

                // Только теперь включаем Wi-Fi
                initWiFiAndEspNow();
                sendSensorData(moisture);

                // ОБЯЗАТЕЛЬНО СБРАСЫВАЕМ счетчик, так как отправка состоялась
                skippedBootsCounter = 0;
            }
            else
            {
                // Если данные НЕ изменились и время "пульса" еще НЕ пришло:
                skippedBootsCounter++; // Увеличиваем счетчик пропущенных шагов в RTC памяти

                Serial.printf("[SYS] Пропуск отправки (%d/%d). Изменение незначительно (Было: %.1f%%, Стало: %.1f%%)\n",
                              skippedBootsCounter, r_maxSkippedBoots, lastSentMoisture, moisture);

                // Имитируем успешное завершение отправки, чтобы сработал уход в сон ниже
                txDone = true;
            }
            dataSentThisBoot = true;
        }

        // Ограничиваем сессию бодрствования (твой старый рабочий код сна)
        if (!forcedAwake)
        {
            if (txDone || currentMillis > 1500)
            {
                Serial.println("[SLEEP] Уходим в глубокий сон...");
                Serial.flush();
                gpio_hold_en((gpio_num_t)BUTTON_PIN);
                esp_deep_sleep_start();
            }
        }
        else
        {
            if (currentMillis > 10000)
            {
                Serial.println("[SLEEP] Время ожидания интерфейса вышло. Уходим в глубокий сон...");
                Serial.flush();
                gpio_hold_en((gpio_num_t)BUTTON_PIN);
                esp_deep_sleep_start();
            }
        }
    }
}
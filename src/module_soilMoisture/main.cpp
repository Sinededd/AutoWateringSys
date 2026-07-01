#include <Arduino.h>
#include <Button2.h>
#include "config.h"
#include "types.h"
#include "sensor.h"
#include "network.h"

// Глобальные объекты и переменные времени выполнения (обнуляются при пробуждении)
Button2 btn;
unsigned long lastTransmission = 0;
unsigned long lastToggle       = 0;
unsigned long rapidBlinkTimer  = 0;

bool ledState         = true;
bool currentState     = true;
bool dataSentThisBoot = false; 
bool forcedAwake      = false; 

// Индикация состояний светодиодом
void updateLedIndicator() { 

    unsigned long currentMillis = millis();

    if (updateMAC && (currentMillis - lastToggle < RESET_BLINK_TIME)) {
        if (currentMillis - rapidBlinkTimer > 175) {
            rapidBlinkTimer = currentMillis;
            ledState = !ledState;
            digitalWrite(LED_PIN, ledState);
        }
        return;
    } else {
        if (updateMAC) { 
            updateMAC = false;
            lastToggle = currentMillis; 
        }
    }

    switch (pairingStatus) {
        case NOT_PAIRED:
        case PAIRING_REQUESTED:
            if (currentMillis - lastToggle >= 500) {
                ledState = !ledState;
                lastToggle = currentMillis;
            }
            break;

        case PAIRED:
            ledState = false; // Выключаем светодиод в режиме сна / бодрствования для экономии батареи
            break;
    }

    if (currentState != ledState) {
        currentState = ledState;
        digitalWrite(LED_PIN, currentState);
    }
}

// Обработчики прерываний кнопки
void handleButtonTap(Button2 &b) {
    Serial.println("Клик кнопки в активном режиме!");
}

void handleLongPressDetected(Button2 &b) {
    Serial.println("[BUTTON] Удержание 5 сек! Сброс сопряжения...");
    resetNetworkSettings();
    updateMAC = true;
    lastToggle = millis();
}

void setup() {
#ifdef DEBUG_ENABLE
    Serial.begin(115200);
    delay(50);
#endif

    // Настройка базовой периферии
    pinMode(LED_PIN, OUTPUT);
    pinMode(SENSOR_POWER_PIN, OUTPUT);
    digitalWrite(SENSOR_POWER_PIN, LOW);
    pinMode(BUTTON_PIN, INPUT_PULLUP); 

    // Определение причины просыпания чипа
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
        Serial.println("[WAKEUP] Пробуждение по нажатию КНОПКИ.");
        forcedAwake = true;
    } else if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
        Serial.println("[WAKEUP] Пробуждение по ТАЙМЕРУ.");
        forcedAwake = false;
    } else {
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

    // Инициализация сетевого стека
    initWiFiAndEspNow();

    // Конфигурация источников пробуждения из Deep Sleep
    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP_SEC * 1000000ULL);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, (esp_deepsleep_gpio_wake_up_mode_t)0);
}

void loop() {
    btn.loop();
    updateLedIndicator();
    unsigned long currentMillis = millis();

    // Режим А: Нет связи — удерживаем плату активной для сопряжения
    if (pairingStatus == NOT_PAIRED) {
        if (currentMillis - lastTransmission >= PAIRING_INTERVAL) {
            lastTransmission = currentMillis;
            sendPairingRequest();
        }
        return; 
    }

    if (pairingStatus == PAIRING_REQUESTED) {
        if (currentMillis - lastTransmission >= PAIRING_INTERVAL) {
            Serial.println("Таймаут сопряжения. Повтор...");
            pairingStatus = NOT_PAIRED;
        }
        return; 
    }

    // Режим Б: Плата сопряжена (PAIRED)
    if (pairingStatus == PAIRED) {
        
        // Отправляем данные один раз за это пробуждение
        if (!dataSentThisBoot) {
            float moisture = readSoilMoisture(); // Считываем датчик (функция из sensor.h)
            sendSensorData(moisture);           // Передаем пакет в сеть (функция из network.h)
            dataSentThisBoot = true;
        }

        // Ограничиваем сессию бодрствования
        if (!forcedAwake) {
            // Проснулись по таймеру: засыпаем сразу по факту доставки пакета или таймауту 1.5 секунды
            if (txDone || currentMillis > 1500) {
                Serial.println("[SLEEP] Данные отправлены по таймеру. Уходим в глубокий сон...");
                Serial.flush();
                esp_deep_sleep_start();
            }
        } else {
            // Проснулись от кнопки / питания: держим плату включенной 6 секунд на случай зажатия сброса
            if (currentMillis > 10000) {
                Serial.println("[SLEEP] Время ожидания интерфейса вышло. Уходим в глубокий сон...");
                Serial.flush();
                esp_deep_sleep_start();
            }
        }
    }
}
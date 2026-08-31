#pragma once

#include <Arduino.h>

namespace config {

// Joystick analog axes.
constexpr uint8_t JOYSTICK_X_ADC_PIN = GPIO_NUM_0;
constexpr uint8_t JOYSTICK_Y_ADC_PIN = GPIO_NUM_1;
constexpr uint8_t ADC_RESOLUTION_BITS = 12;
constexpr uint16_t ADC_MAX_READING = 4095;
constexpr uint8_t JOYSTICK_SAMPLE_COUNT = 5;
constexpr uint8_t JOYSTICK_SAMPLE_DELAY_MS = 2;
constexpr bool JOYSTICK_X_REVERSED = false;
constexpr bool JOYSTICK_Y_REVERSED = false;

// Buttons are wired between the GPIO and GND, using the internal pull-up.
// Names follow the existing STA1 remote-control protocol.
constexpr uint8_t JOYSTICK_BUTTON_PIN = GPIO_NUM_3;
constexpr uint8_t LEFT_BLINK_BUTTON_PIN = GPIO_NUM_4;   // LB
constexpr uint8_t RIGHT_BLINK_BUTTON_PIN = GPIO_NUM_5;  // RB
constexpr uint8_t R1_BUTTON_PIN = GPIO_NUM_6;
constexpr uint8_t R2_BUTTON_PIN = GPIO_NUM_7;
// The legacy linked-blink switch is intentionally omitted on Mini.
// GPIO4 and GPIO5 always control the two eyelids independently.
constexpr uint8_t BUTTON_COUNT = 5;
constexpr uint8_t BUTTON_PINS[BUTTON_COUNT] = {
    JOYSTICK_BUTTON_PIN,
    LEFT_BLINK_BUTTON_PIN,
    RIGHT_BLINK_BUTTON_PIN,
    R1_BUTTON_PIN,
    R2_BUTTON_PIN,
};

// Broadcast matches the STA1 pairing flow. Replace with a receiver MAC for unicast.
constexpr uint8_t RECEIVER_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
constexpr uint32_t ESPNOW_SEND_INTERVAL_MS = 10;
constexpr uint8_t ESPNOW_WIFI_CHANNEL = 1;

constexpr bool ENABLE_SERIAL_DIAGNOSTICS = true;
constexpr uint32_t SERIAL_DIAGNOSTICS_INTERVAL_MS = 1000;

constexpr uint32_t LONG_PRESS_MS = 3000;
constexpr uint32_t MULTI_PRESS_WINDOW_MS = 1000;
constexpr uint32_t PAIRING_RETRY_INTERVAL_MS = 500;

constexpr uint16_t EEPROM_SIZE = 32;
constexpr uint8_t EEPROM_MAGIC_ADDRESS = 0;
constexpr uint8_t EEPROM_X_CENTER_ADDRESS = 1;
constexpr uint8_t EEPROM_Y_CENTER_ADDRESS = 2;
constexpr uint8_t EEPROM_MAGIC = 55;

// The Mini has no light sensor in the current pin allocation.
constexpr float DEFAULT_LIGHT_VALUE = 1.0f;

}  // namespace config

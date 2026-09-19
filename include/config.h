#pragma once

#include <Arduino.h>

namespace config {

// Joystick analog axes.
constexpr uint8_t JOYSTICK_X_ADC_PIN = GPIO_NUM_1;
constexpr uint8_t JOYSTICK_Y_ADC_PIN = GPIO_NUM_0;
constexpr uint8_t ADC_RESOLUTION_BITS = 12;
constexpr uint16_t ADC_MAX_READING = 4095;
// The joystick is powered directly from a regulated 1.8 V rail. Keep the
// widest C3 attenuation so the full physical span always remains in range.
constexpr adc_attenuation_t JOYSTICK_ADC_ATTENUATION = ADC_11db;
constexpr uint8_t JOYSTICK_SAMPLE_COUNT = 16;
constexpr uint8_t JOYSTICK_SAMPLE_DELAY_MS = 0;
// Stronger low-pass filtering for the Switch joystick's small ADC jitter.
constexpr uint8_t JOYSTICK_FILTER_DIVISOR = 8;
// Deadband is expressed in raw ADC counts, before the range mapping.
constexpr uint16_t JOYSTICK_FILTER_DEADBAND = 16;
constexpr uint8_t JOYSTICK_OUTPUT_DEADBAND = 2;
constexpr uint16_t JOYSTICK_RANGE_CALIBRATION_MIN_SPAN = 800;
// Endpoint updates are committed after a quiet period, with a maximum delay
// while the user is moving the stick. This avoids excessive EEPROM writes.
constexpr uint32_t JOYSTICK_CALIBRATION_SAVE_DELAY_MS = 1500;
constexpr uint32_t JOYSTICK_CALIBRATION_SAVE_MAX_INTERVAL_MS = 8000;
// With the current GPIO1/GPIO0 wiring, normalized X/Y already match the
// protocol directions. Swapping axes and reversing X caused up -> left and
// left -> down. Apply orientation only after physical ADC calibration.
constexpr bool JOYSTICK_SWAP_XY = false;
constexpr bool JOYSTICK_X_REVERSED = false;
constexpr bool JOYSTICK_Y_REVERSED = false;

// Buttons are wired between the GPIO and GND, using the internal pull-up.
// Names follow the existing STA1 remote-control protocol.
constexpr uint8_t JOYSTICK_BUTTON_PIN = GPIO_NUM_2;
constexpr uint8_t LEFT_BLINK_BUTTON_PIN = GPIO_NUM_4;   // LB
constexpr uint8_t RIGHT_BLINK_BUTTON_PIN = GPIO_NUM_5;  // RB
constexpr uint8_t R1_BUTTON_PIN = GPIO_NUM_7;
constexpr uint8_t R2_BUTTON_PIN = GPIO_NUM_6;
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

// Battery sense: battery -> 110 kOhm -> GPIO3 -> 56 kOhm -> GND.
constexpr uint8_t BATTERY_ADC_PIN = GPIO_NUM_3;
constexpr adc_attenuation_t BATTERY_ADC_ATTENUATION = ADC_11db;
constexpr uint8_t BATTERY_SAMPLE_COUNT = 16;
constexpr uint32_t BATTERY_MEASURE_INTERVAL_MS = 1000;
constexpr float BATTERY_DIVIDER_HIGH_OHMS = 110000.0f;
constexpr float BATTERY_DIVIDER_LOW_OHMS = 56000.0f;

// One addressable status LED. Brightness is deliberately kept at a moderate
// level so battery indication remains readable without becoming distracting.
constexpr uint8_t STATUS_LED_PIN = GPIO_NUM_10;
constexpr uint16_t STATUS_LED_COUNT = 1;
constexpr uint8_t STATUS_LED_BRIGHTNESS = 64;

// All current ESP-NOW traffic uses the fixed channel 1. Broadcast lets one
// Mini address every bound device on that channel.
constexpr uint8_t RECEIVER_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
constexpr uint32_t ESPNOW_SEND_INTERVAL_MS = 10;
constexpr uint8_t ESPNOW_WIFI_CHANNEL = 1;
// Legacy discovery/data names remain aliases for shared channel helpers.
constexpr uint8_t ESPNOW_DISCOVERY_CHANNEL = ESPNOW_WIFI_CHANNEL;
constexpr uint8_t ESPNOW_DATA_CHANNELS[] = {ESPNOW_WIFI_CHANNEL};
constexpr uint8_t ESPNOW_DATA_CHANNEL_COUNT =
    sizeof(ESPNOW_DATA_CHANNELS) / sizeof(ESPNOW_DATA_CHANNELS[0]);
constexpr uint32_t PAIRING_RETRY_INTERVAL_MS = 500;

constexpr bool ENABLE_SERIAL_DIAGNOSTICS = true;
constexpr uint32_t SERIAL_DIAGNOSTICS_INTERVAL_MS = 1000;
constexpr uint32_t SERIAL_BAUD_RATE = 921600;
// Temporary high-rate electrical diagnostics for the Switch joystick.
// Set false after the hardware check to silence the 50 Hz raw-data stream.
constexpr bool ENABLE_JOYSTICK_RAW_DIAGNOSTICS = true;
constexpr uint32_t JOYSTICK_RAW_DIAGNOSTICS_INTERVAL_MS = 10;
// The 4.7 kOhm hardware change is in place; restore normal control traffic.
constexpr bool JOYSTICK_DIAGNOSTIC_ONLY = false;

constexpr uint32_t LONG_PRESS_MS = 3000;
constexpr uint32_t MULTI_PRESS_WINDOW_MS = 1000;

constexpr uint16_t EEPROM_SIZE = 32;
constexpr uint8_t EEPROM_MAGIC_ADDRESS = 0;
constexpr uint8_t EEPROM_X_CENTER_ADDRESS = 1;
constexpr uint8_t EEPROM_X_MIN_ADDRESS = 3;
constexpr uint8_t EEPROM_X_MAX_ADDRESS = 5;
constexpr uint8_t EEPROM_Y_CENTER_ADDRESS = 7;
constexpr uint8_t EEPROM_Y_MIN_ADDRESS = 9;
constexpr uint8_t EEPROM_Y_MAX_ADDRESS = 11;
constexpr uint8_t EEPROM_EYELID_LINKED_ADDRESS = 13;
// Rev 57 invalidates center/range data recorded with the previous X/Y pinout.
constexpr uint8_t EEPROM_MAGIC = 57;
constexpr uint8_t EYELID_CONTROL_INDEPENDENT = 0;
constexpr uint8_t EYELID_CONTROL_LINKED = 1;

// The Mini has no light sensor in the current pin allocation.
constexpr float DEFAULT_LIGHT_VALUE = 1.0f;

}  // namespace config

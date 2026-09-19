#pragma once

#include <Arduino.h>

namespace status_led {

enum class PairingState : uint8_t {
  INACTIVE,
  PENDING,
  ACTIVE,
};

enum class Event : uint8_t {
  STARTUP,
  MODE_MANUAL,
  MODE_AUTO_FULL,
  MODE_AUTO_BLINK,
  LINKED_ON,
  LINKED_OFF,
  LOCKED,
  UNLOCKED,
  CALIBRATED,
  PREVIOUS_EYE,
  NEXT_EYE,
};

void init();
void notify(Event event);
void update(uint32_t now,
            float batteryVoltage,
            PairingState pairingState,
            bool radioReady);

}  // namespace status_led

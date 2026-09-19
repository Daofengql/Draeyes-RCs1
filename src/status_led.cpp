#include "status_led.h"

#include <Adafruit_NeoPixel.h>

#include "config.h"

namespace {

Adafruit_NeoPixel statusLed(config::STATUS_LED_COUNT,
                            config::STATUS_LED_PIN,
                            NEO_GRB + NEO_KHZ800);

struct Rgb {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

status_led::Event activeEvent = status_led::Event::STARTUP;
uint32_t activeEventStartedAt = 0;
bool activeEventPending = false;
uint32_t lastRefreshAt = 0;

constexpr uint32_t REFRESH_INTERVAL_MS = 20;
constexpr uint32_t PAIRING_PENDING_PHASE_MS = 500;
constexpr uint32_t PAIRING_ACTIVE_PHASE_MS = 120;
constexpr uint32_t RADIO_ERROR_PHASE_MS = 350;
constexpr uint32_t LOW_BATTERY_PHASE_MS = 250;

Rgb eventColor(status_led::Event event) {
  switch (event) {
    case status_led::Event::STARTUP:
      return {255, 255, 255};
    case status_led::Event::MODE_MANUAL:
      return {0, 80, 255};
    case status_led::Event::MODE_AUTO_FULL:
      return {150, 0, 255};
    case status_led::Event::MODE_AUTO_BLINK:
      return {0, 180, 255};
    case status_led::Event::LINKED_ON:
      return {255, 0, 160};
    case status_led::Event::LINKED_OFF:
      return {90, 0, 60};
    case status_led::Event::LOCKED:
      return {255, 255, 255};
    case status_led::Event::UNLOCKED:
      return {90, 90, 90};
    case status_led::Event::CALIBRATED:
      return {0, 255, 80};
    case status_led::Event::PREVIOUS_EYE:
      return {255, 120, 0};
    case status_led::Event::NEXT_EYE:
      return {255, 60, 0};
  }
  return {255, 255, 255};
}

bool pulseOn(uint32_t elapsed, uint16_t phaseMs, uint8_t pulseCount) {
  const uint32_t phase = elapsed / phaseMs;
  return phase < (static_cast<uint32_t>(pulseCount) * 2U) &&
         (phase % 2U) == 0U;
}

bool eventOutputOn(status_led::Event event, uint32_t elapsed) {
  switch (event) {
    case status_led::Event::STARTUP:
      return true;
    case status_led::Event::MODE_MANUAL:
    case status_led::Event::MODE_AUTO_FULL:
    case status_led::Event::MODE_AUTO_BLINK:
      return pulseOn(elapsed, 120, 3);
    case status_led::Event::LINKED_ON:
    case status_led::Event::LINKED_OFF:
      return pulseOn(elapsed, 130, 2);
    case status_led::Event::LOCKED:
      return pulseOn(elapsed, 110, 3);
    case status_led::Event::UNLOCKED:
      return pulseOn(elapsed, 110, 1);
    case status_led::Event::CALIBRATED:
      return pulseOn(elapsed, 100, 4);
    case status_led::Event::PREVIOUS_EYE:
      return true;
    case status_led::Event::NEXT_EYE:
      return pulseOn(elapsed, 100, 2);
  }
  return false;
}

uint32_t eventDuration(status_led::Event event) {
  switch (event) {
    case status_led::Event::STARTUP:
      return 500;
    case status_led::Event::MODE_MANUAL:
    case status_led::Event::MODE_AUTO_FULL:
    case status_led::Event::MODE_AUTO_BLINK:
      return 720;
    case status_led::Event::LINKED_ON:
    case status_led::Event::LINKED_OFF:
      return 520;
    case status_led::Event::LOCKED:
      return 660;
    case status_led::Event::UNLOCKED:
      return 220;
    case status_led::Event::CALIBRATED:
      return 800;
    case status_led::Event::PREVIOUS_EYE:
      return 220;
    case status_led::Event::NEXT_EYE:
      return 400;
  }
  return 0;
}

Rgb batteryColor(float batteryVoltage) {
  if (batteryVoltage >= 4.00f) {
    return {0, 255, 0};
  }
  if (batteryVoltage >= 3.80f) {
    return {170, 255, 0};
  }
  if (batteryVoltage >= 3.60f) {
    return {255, 160, 0};
  }
  return {255, 0, 0};
}

void showColor(Rgb color, bool enabled) {
  if (!enabled) {
    statusLed.clear();
  } else {
    statusLed.setPixelColor(
        0, statusLed.Color(color.red, color.green, color.blue));
  }
  statusLed.show();
}

}  // namespace

namespace status_led {

void init() {
  statusLed.begin();
  statusLed.setBrightness(config::STATUS_LED_BRIGHTNESS);
  statusLed.clear();
  statusLed.show();
}

void notify(Event event) {
  activeEvent = event;
  activeEventStartedAt = millis();
  activeEventPending = true;
}

void update(uint32_t now,
            float batteryVoltage,
            PairingState pairingState,
            bool radioReady) {
  if (now - lastRefreshAt < REFRESH_INTERVAL_MS) {
    return;
  }
  lastRefreshAt = now;

  if (!radioReady) {
    const bool enabled = ((now / RADIO_ERROR_PHASE_MS) % 2U) == 0U;
    showColor({255, 0, 80}, enabled);
    return;
  }

  if (pairingState != PairingState::INACTIVE) {
    const uint32_t phaseMs = pairingState == PairingState::ACTIVE
                                 ? PAIRING_ACTIVE_PHASE_MS
                                 : PAIRING_PENDING_PHASE_MS;
    const bool enabled = ((now / phaseMs) % 2U) == 0U;
    showColor({0, 100, 255}, enabled);
    return;
  }

  if (activeEventPending) {
    const uint32_t elapsed = now - activeEventStartedAt;
    if (elapsed < eventDuration(activeEvent)) {
      showColor(eventColor(activeEvent), eventOutputOn(activeEvent, elapsed));
      return;
    }
    activeEventPending = false;
  }

  const bool lowBattery = batteryVoltage < 3.40f;
  const bool enabled = !lowBattery ||
                       ((now / LOW_BATTERY_PHASE_MS) % 2U) == 0U;
  showColor(batteryColor(batteryVoltage), enabled);
}

}  // namespace status_led

#include <Arduino.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "config.h"
#include "eye_animation.h"
#include "remote_protocol.h"
#include "status_led.h"

namespace {

enum class ControlMode : uint8_t {
  MANUAL_MODE,
  AUTO_FULL_MODE,
  AUTO_BLINK_MODE,
};

ControlMode currentMode = ControlMode::MANUAL_MODE;
bool joystickLocked = false;
uint8_t lockedJoystickX = 127;
uint8_t lockedJoystickY = 127;
bool eyelidControlLinked = false;

int joystickXRead = 127;
int joystickYRead = 127;
uint16_t joystickXRawCenter = config::ADC_MAX_READING / 2;
uint16_t joystickYRawCenter = config::ADC_MAX_READING / 2;
uint16_t joystickXRawMin = 0;
uint16_t joystickXRawMax = config::ADC_MAX_READING;
uint16_t joystickYRawMin = 0;
uint16_t joystickYRawMax = config::ADC_MAX_READING;
uint16_t joystickXRawSample = 0;
uint16_t joystickYRawSample = 0;
uint16_t joystickXRawFiltered = 0;
uint16_t joystickYRawFiltered = 0;
uint32_t joystickXMillivolts = 0;
uint32_t joystickYMillivolts = 0;
bool joystickFilterInitialized = false;

bool lastR1 = false;
bool lastJoystickButton = false;
bool lastLeftButton = false;
bool lastRightButton = false;
bool lastLeftEyelidPressed = false;
bool lastRightEyelidPressed = false;
bool lastLinkedEyelidPressed = false;

uint32_t lastPressTimeR1 = 0;
uint8_t pressCountR1 = 0;

uint32_t r1LongPressStartedAt = 0;
uint32_t r2LongPressStartedAt = 0;
uint32_t pairingStartedAt = 0;
uint32_t lastPairingSendAt = 0;
uint32_t calibrationStartedAt = 0;
bool r1LongPressTracking = false;
bool r2LongPressTracking = false;
bool pairingTracking = false;
bool calibrationTracking = false;
bool r1LongPressHandled = false;
bool r2LongPressHandled = false;
bool pairingHandled = false;
bool calibrationHandled = false;
bool joystickCalibrationSavePending = false;
uint32_t joystickCalibrationLastChangeAt = 0;
uint32_t joystickCalibrationLastSaveAt = 0;

float leftBlinkValues[8] = {};
float rightBlinkValues[8] = {};
float linkedBlinkValues[8] = {};
uint8_t leftBlinkCount = 0;
uint8_t rightBlinkCount = 0;
uint8_t linkedBlinkCount = 0;
uint8_t leftBlinkIndex = 0;
uint8_t rightBlinkIndex = 0;
uint8_t linkedBlinkIndex = 0;
bool leftBlinkActive = false;
bool rightBlinkActive = false;
bool linkedBlinkActive = false;
float currentLeftBlink = 1.0f;
float currentRightBlink = 1.0f;

eye_animation::EyeState eyeState = {};
uint32_t lastSendAt = 0;
uint32_t lastDiagnosticsAt = 0;
uint32_t lastJoystickRawDiagnosticsAt = 0;
uint32_t lastBatteryMeasureAt = 0;
uint16_t batteryAdcMillivolts = 0;
float batteryVoltage = 0.0f;
  bool espNowReady = false;
  bool eepromReady = false;

  uint8_t activeWifiChannel = config::ESPNOW_DISCOVERY_CHANNEL;
uint8_t pairingRequestedChannel = 0;
uint32_t pairingNonce = 0;

bool isPressed(uint8_t pin) {
  return digitalRead(pin) == LOW;
}

bool isDataChannel(uint8_t channel) {
  for (uint8_t index = 0; index < config::ESPNOW_DATA_CHANNEL_COUNT; ++index) {
    if (config::ESPNOW_DATA_CHANNELS[index] == channel) {
      return true;
    }
  }
  return false;
}

uint16_t readAxisAverage(uint8_t pin) {
  uint32_t total = 0;

  for (uint8_t sample = 0; sample < config::JOYSTICK_SAMPLE_COUNT; ++sample) {
    total += static_cast<uint32_t>(analogRead(pin));
    if (sample + 1 < config::JOYSTICK_SAMPLE_COUNT) {
      delay(config::JOYSTICK_SAMPLE_DELAY_MS);
    }
  }

  return static_cast<uint16_t>(total / config::JOYSTICK_SAMPLE_COUNT);
}

uint16_t readMilliVoltsAverage(uint8_t pin, uint8_t count) {
  uint32_t total = 0;
  for (uint8_t sample = 0; sample < count; ++sample) {
    total += analogReadMilliVolts(pin);
  }
  return static_cast<uint16_t>(total / count);
}

void updateBatteryMeasurement(uint32_t now, bool force = false) {
  if (!force && now - lastBatteryMeasureAt <
                   config::BATTERY_MEASURE_INTERVAL_MS) {
    return;
  }

  lastBatteryMeasureAt = now;
  batteryAdcMillivolts = readMilliVoltsAverage(
      config::BATTERY_ADC_PIN, config::BATTERY_SAMPLE_COUNT);
  const float dividerRatio =
      (config::BATTERY_DIVIDER_HIGH_OHMS +
       config::BATTERY_DIVIDER_LOW_OHMS) /
      config::BATTERY_DIVIDER_LOW_OHMS;
  batteryVoltage =
      (static_cast<float>(batteryAdcMillivolts) / 1000.0f) * dividerRatio;
}

uint16_t filterAxisReading(uint16_t sample, uint16_t &filtered) {
  if (!joystickFilterInitialized) {
    filtered = sample;
    return filtered;
  }

  const int32_t delta = static_cast<int32_t>(sample) - filtered;
  if (delta >= -static_cast<int32_t>(config::JOYSTICK_FILTER_DEADBAND) &&
      delta <= static_cast<int32_t>(config::JOYSTICK_FILTER_DEADBAND)) {
    return filtered;
  }

  int32_t step = delta / config::JOYSTICK_FILTER_DIVISOR;
  if (step == 0) {
    step = delta > 0 ? 1 : -1;
  }
  filtered = static_cast<uint16_t>(constrain(
      static_cast<int32_t>(filtered) + step, 0,
      static_cast<int32_t>(config::ADC_MAX_READING)));
  return filtered;
}

uint8_t mapRawAxis(uint16_t raw,
                   uint16_t rawMin,
                   uint16_t rawCenter,
                   uint16_t rawMax) {
  rawMin = constrain(rawMin, 0, config::ADC_MAX_READING);
  rawCenter = constrain(rawCenter, 1, config::ADC_MAX_READING - 1);
  rawMax = constrain(rawMax, 1, config::ADC_MAX_READING);

  // Endpoints are learned progressively. A side is usable only after it has
  // moved far enough from the center; otherwise synthesize that side so small
  // center noise cannot be amplified into a large output change.
  const bool minSideUsable =
      rawMin < rawCenter &&
      rawCenter - rawMin >= config::JOYSTICK_RANGE_CALIBRATION_MIN_SPAN;
  if (!minSideUsable) {
    rawMin = rawCenter > config::JOYSTICK_RANGE_CALIBRATION_MIN_SPAN
                 ? rawCenter - config::JOYSTICK_RANGE_CALIBRATION_MIN_SPAN
                 : 0;
  }

  const bool maxSideUsable =
      rawMax > rawCenter &&
      rawMax - rawCenter >= config::JOYSTICK_RANGE_CALIBRATION_MIN_SPAN;
  if (!maxSideUsable) {
    const uint32_t fallbackMax =
        static_cast<uint32_t>(rawCenter) +
        config::JOYSTICK_RANGE_CALIBRATION_MIN_SPAN;
    rawMax = fallbackMax < config::ADC_MAX_READING
                 ? static_cast<uint16_t>(fallbackMax)
                 : config::ADC_MAX_READING;
  }

  if (raw <= rawCenter) {
    return static_cast<uint8_t>(constrain(
        map(raw, rawMin, rawCenter, 255, 127), 127, 255));
  }
  return static_cast<uint8_t>(constrain(
      map(raw, rawCenter, rawMax, 127, 0), 0, 127));
}

void readJoystickData() {
  joystickXRawSample = readAxisAverage(config::JOYSTICK_X_ADC_PIN);
  joystickYRawSample = readAxisAverage(config::JOYSTICK_Y_ADC_PIN);
  joystickXMillivolts = analogReadMilliVolts(config::JOYSTICK_X_ADC_PIN);
  joystickYMillivolts = analogReadMilliVolts(config::JOYSTICK_Y_ADC_PIN);

  joystickXRawFiltered = filterAxisReading(
      joystickXRawSample, joystickXRawFiltered);
  joystickYRawFiltered = filterAxisReading(
      joystickYRawSample, joystickYRawFiltered);
  joystickFilterInitialized = true;

  joystickXRead = mapRawAxis(joystickXRawFiltered,
                             joystickXRawMin,
                             joystickXRawCenter,
                             joystickXRawMax);
  joystickYRead = mapRawAxis(joystickYRawFiltered,
                             joystickYRawMin,
                             joystickYRawCenter,
                             joystickYRawMax);
}

int mapCentered(int value, int center, bool reversed) {
  center = constrain(center, 1, 254);
  value = constrain(value, 0, 255);

  if (abs(value - center) <= config::JOYSTICK_OUTPUT_DEADBAND) {
    value = center;
  }

  int mapped = value < center
                   ? map(value, 0, center, 0, 127)
                   : map(value, center, 255, 127, 255);
  mapped = constrain(mapped, 0, 255);
  return reversed ? 255 - mapped : mapped;
}

int getLogicalJoystickXRead() {
  return config::JOYSTICK_SWAP_XY ? joystickYRead : joystickXRead;
}

int getLogicalJoystickYRead() {
  return config::JOYSTICK_SWAP_XY ? joystickXRead : joystickYRead;
}

void generateBlinkArray(float start, float stop, uint8_t count, float *buffer) {
  if (buffer == nullptr || count == 0) {
    return;
  }

  if (count == 1) {
    buffer[0] = start;
    return;
  }

  const float step = (stop - start) / static_cast<float>(count - 1);
  for (uint8_t index = 0; index + 1 < count; ++index) {
    buffer[index] = start + step * index;
  }
  buffer[count - 1] = stop;
}

void startLeftBlink(bool pressed) {
  const uint8_t count = pressed ? random(6, 8) : random(7, 9);
  generateBlinkArray(pressed ? 1.0f : 0.0f,
                     pressed ? 0.0f : 1.0f,
                     count,
                     leftBlinkValues);
  leftBlinkCount = count;
  leftBlinkIndex = 0;
  leftBlinkActive = true;
}

void startRightBlink(bool pressed) {
  const uint8_t count = pressed ? random(3, 5) : random(7, 9);
  generateBlinkArray(pressed ? 1.0f : 0.0f,
                     pressed ? 0.0f : 1.0f,
                     count,
                     rightBlinkValues);
  rightBlinkCount = count;
  rightBlinkIndex = 0;
  rightBlinkActive = true;
}

void startLinkedBlink(bool pressed) {
  const uint8_t count = pressed ? random(6, 8) : random(7, 9);
  generateBlinkArray(pressed ? 1.0f : 0.0f,
                     pressed ? 0.0f : 1.0f,
                     count,
                     linkedBlinkValues);
  linkedBlinkCount = count;
  linkedBlinkIndex = 0;
  linkedBlinkActive = true;
}

void advanceManualBlinkEffects(bool leftPressed,
                               bool rightPressed,
                               bool joystickButtonPressed) {
  if (currentMode != ControlMode::MANUAL_MODE || joystickButtonPressed) {
    lastLeftEyelidPressed = false;
    lastRightEyelidPressed = false;
    lastLinkedEyelidPressed = false;
    return;
  }

  if (eyelidControlLinked) {
    if (leftPressed != lastLinkedEyelidPressed) {
      startLinkedBlink(leftPressed);
    }
    lastLinkedEyelidPressed = leftPressed;
    lastLeftEyelidPressed = false;
    lastRightEyelidPressed = false;

    if (linkedBlinkActive) {
      if (linkedBlinkIndex < linkedBlinkCount) {
        const float value = linkedBlinkValues[linkedBlinkIndex++];
        currentLeftBlink = value;
        currentRightBlink = value;
      } else {
        linkedBlinkActive = false;
      }
    }
    return;
  }

  if (leftPressed != lastLeftEyelidPressed) {
    startLeftBlink(leftPressed);
  }
  if (rightPressed != lastRightEyelidPressed) {
    startRightBlink(rightPressed);
  }
  lastLeftEyelidPressed = leftPressed;
  lastRightEyelidPressed = rightPressed;
  lastLinkedEyelidPressed = false;

  if (leftBlinkActive) {
    if (leftBlinkIndex < leftBlinkCount) {
      currentLeftBlink = leftBlinkValues[leftBlinkIndex++];
    } else {
      leftBlinkActive = false;
    }
  }
  if (rightBlinkActive) {
    if (rightBlinkIndex < rightBlinkCount) {
      currentRightBlink = rightBlinkValues[rightBlinkIndex++];
    } else {
      rightBlinkActive = false;
    }
  }
}

void updateAnimation() {
  if (currentMode == ControlMode::AUTO_FULL_MODE) {
    eye_animation::updateAutoMovement(&eyeState, 3000);
  }

  if (currentMode != ControlMode::MANUAL_MODE) {
    eye_animation::updateAutoBlink(&eyeState);
    eye_animation::updateBlinkState(&eyeState);
  }
}

const char *modeName(ControlMode mode) {
  switch (mode) {
    case ControlMode::MANUAL_MODE:
      return "手动";
    case ControlMode::AUTO_FULL_MODE:
      return "全自动";
    case ControlMode::AUTO_BLINK_MODE:
      return "自动眨眼";
  }
  return "未知";
}

void cycleMode() {
  switch (currentMode) {
    case ControlMode::MANUAL_MODE:
      currentMode = ControlMode::AUTO_FULL_MODE;
      break;
    case ControlMode::AUTO_FULL_MODE:
      currentMode = ControlMode::AUTO_BLINK_MODE;
      break;
    case ControlMode::AUTO_BLINK_MODE:
      currentMode = ControlMode::MANUAL_MODE;
      break;
  }

  Serial.print("模式: ");
  Serial.println(modeName(currentMode));

  switch (currentMode) {
    case ControlMode::MANUAL_MODE:
      status_led::notify(status_led::Event::MODE_MANUAL);
      break;
    case ControlMode::AUTO_FULL_MODE:
      status_led::notify(status_led::Event::MODE_AUTO_FULL);
      break;
    case ControlMode::AUTO_BLINK_MODE:
      status_led::notify(status_led::Event::MODE_AUTO_BLINK);
      break;
  }
}

void saveJoystickCalibration() {
  if (!eepromReady) {
    return;
  }

  EEPROM.put(config::EEPROM_X_CENTER_ADDRESS, joystickXRawCenter);
  EEPROM.put(config::EEPROM_X_MIN_ADDRESS, joystickXRawMin);
  EEPROM.put(config::EEPROM_X_MAX_ADDRESS, joystickXRawMax);
  EEPROM.put(config::EEPROM_Y_CENTER_ADDRESS, joystickYRawCenter);
  EEPROM.put(config::EEPROM_Y_MIN_ADDRESS, joystickYRawMin);
  EEPROM.put(config::EEPROM_Y_MAX_ADDRESS, joystickYRawMax);
  EEPROM.write(config::EEPROM_MAGIC_ADDRESS, config::EEPROM_MAGIC);
  EEPROM.commit();
  joystickCalibrationSavePending = false;
  joystickCalibrationLastSaveAt = millis();
}

bool validJoystickCenter(uint16_t rawCenter) {
  return rawCenter > 0 && rawCenter < config::ADC_MAX_READING;
}

void requestJoystickCalibrationSave(uint32_t now) {
  joystickCalibrationSavePending = true;
  joystickCalibrationLastChangeAt = now;
}

void serviceJoystickCalibrationPersistence(uint32_t now) {
  if (!joystickCalibrationSavePending) {
    return;
  }

  const bool quiet = now - joystickCalibrationLastChangeAt >=
                     config::JOYSTICK_CALIBRATION_SAVE_DELAY_MS;
  const bool overdue = now - joystickCalibrationLastSaveAt >=
                       config::JOYSTICK_CALIBRATION_SAVE_MAX_INTERVAL_MS;
  if (!quiet && !overdue) {
    return;
  }

  saveJoystickCalibration();
  Serial.printf("[joystick] bounds saved X=%u/%u/%u Y=%u/%u/%u\n",
                joystickXRawMin,
                joystickXRawCenter,
                joystickXRawMax,
                joystickYRawMin,
                joystickYRawCenter,
                joystickYRawMax);
}

void updateJoystickCalibrationBounds(uint32_t now) {
  bool changed = false;

  // Use filtered readings so small ADC noise cannot expand the stored range.
  // Bounds only expand; normal control is never paused for calibration.
  if (joystickXRawFiltered < joystickXRawMin) {
    joystickXRawMin = joystickXRawFiltered;
    changed = true;
  }
  if (joystickXRawFiltered > joystickXRawMax) {
    joystickXRawMax = joystickXRawFiltered;
    changed = true;
  }
  if (joystickYRawFiltered < joystickYRawMin) {
    joystickYRawMin = joystickYRawFiltered;
    changed = true;
  }
  if (joystickYRawFiltered > joystickYRawMax) {
    joystickYRawMax = joystickYRawFiltered;
    changed = true;
  }

  if (changed) {
    requestJoystickCalibrationSave(now);
  }
  serviceJoystickCalibrationPersistence(now);
}

void calibrateJoystick() {
  // The four-button gesture only establishes the center. Endpoint learning
  // continues in the ordinary loop, so calibration never enters a capture
  // state or interrupts control traffic.
  joystickXRawCenter = constrain(joystickXRawFiltered, 1,
                                 config::ADC_MAX_READING - 1);
  joystickYRawCenter = constrain(joystickYRawFiltered, 1,
                                 config::ADC_MAX_READING - 1);

  // Keep learned endpoints when they still surround the new center. If an
  // axis has no usable range yet, start that axis at the center and let normal
  // movement expand it.
  if (joystickXRawMin >= joystickXRawCenter ||
      joystickXRawMax <= joystickXRawCenter) {
    joystickXRawMin = joystickXRawCenter;
    joystickXRawMax = joystickXRawCenter;
  }
  if (joystickYRawMin >= joystickYRawCenter ||
      joystickYRawMax <= joystickYRawCenter) {
    joystickYRawMin = joystickYRawCenter;
    joystickYRawMax = joystickYRawCenter;
  }

  saveJoystickCalibration();
  status_led::notify(status_led::Event::CALIBRATED);
  Serial.printf("[joystick] center saved X=%u Y=%u; endpoints learn during normal use\n",
                joystickXRawCenter,
                joystickYRawCenter);
}

void initializeCalibration() {
  eepromReady = EEPROM.begin(config::EEPROM_SIZE);
  if (!eepromReady) {
    readJoystickData();
    joystickXRawCenter = constrain(joystickXRawFiltered, 1,
                                   config::ADC_MAX_READING - 1);
    joystickYRawCenter = constrain(joystickYRawFiltered, 1,
                                   config::ADC_MAX_READING - 1);
    joystickXRawMin = joystickXRawCenter;
    joystickXRawMax = joystickXRawCenter;
    joystickYRawMin = joystickYRawCenter;
    joystickYRawMax = joystickYRawCenter;
    Serial.println("EEPROM initialization failed; using runtime joystick calibration");
    return;
  }

  const bool hasStoredCalibration =
      EEPROM.read(config::EEPROM_MAGIC_ADDRESS) == config::EEPROM_MAGIC;
  if (hasStoredCalibration) {
    EEPROM.get(config::EEPROM_X_CENTER_ADDRESS, joystickXRawCenter);
    EEPROM.get(config::EEPROM_X_MIN_ADDRESS, joystickXRawMin);
    EEPROM.get(config::EEPROM_X_MAX_ADDRESS, joystickXRawMax);
    EEPROM.get(config::EEPROM_Y_CENTER_ADDRESS, joystickYRawCenter);
    EEPROM.get(config::EEPROM_Y_MIN_ADDRESS, joystickYRawMin);
    EEPROM.get(config::EEPROM_Y_MAX_ADDRESS, joystickYRawMax);
  }

  const bool storedCentersValid =
      hasStoredCalibration && validJoystickCenter(joystickXRawCenter) &&
      validJoystickCenter(joystickYRawCenter);
  if (!storedCentersValid) {
    readJoystickData();
    joystickXRawCenter = constrain(joystickXRawFiltered, 1,
                                   config::ADC_MAX_READING - 1);
    joystickYRawCenter = constrain(joystickYRawFiltered, 1,
                                   config::ADC_MAX_READING - 1);
    joystickXRawMin = joystickXRawCenter;
    joystickXRawMax = joystickXRawCenter;
    joystickYRawMin = joystickYRawCenter;
    joystickYRawMax = joystickYRawCenter;
    saveJoystickCalibration();
    Serial.println("[joystick] initialized; center saved, endpoints learn during use");
  } else {
    // The previous firmware used 0..4095 as a fallback range. Convert that
    // legacy default to runtime-learned bounds so it can be refined naturally.
    if (joystickXRawMin == 0 && joystickXRawMax == config::ADC_MAX_READING) {
      joystickXRawMin = joystickXRawCenter;
      joystickXRawMax = joystickXRawCenter;
      requestJoystickCalibrationSave(millis());
    }
    if (joystickYRawMin == 0 && joystickYRawMax == config::ADC_MAX_READING) {
      joystickYRawMin = joystickYRawCenter;
      joystickYRawMax = joystickYRawCenter;
      requestJoystickCalibrationSave(millis());
    }
    if (joystickXRawMin > joystickXRawCenter) {
      joystickXRawMin = joystickXRawCenter;
    }
    if (joystickXRawMax < joystickXRawCenter) {
      joystickXRawMax = joystickXRawCenter;
    }
    if (joystickYRawMin > joystickYRawCenter) {
      joystickYRawMin = joystickYRawCenter;
    }
    if (joystickYRawMax < joystickYRawCenter) {
      joystickYRawMax = joystickYRawCenter;
    }
  }

  Serial.printf("[joystick] loaded X=%u/%u/%u Y=%u/%u/%u\n",
                joystickXRawMin,
                joystickXRawCenter,
                joystickXRawMax,
                joystickYRawMin,
                joystickYRawCenter,
                joystickYRawMax);

  eyelidControlLinked =
      EEPROM.read(config::EEPROM_EYELID_LINKED_ADDRESS) ==
      config::EYELID_CONTROL_LINKED;
}

void saveEyelidControlMode() {
  if (!eepromReady) {
    return;
  }
  EEPROM.write(config::EEPROM_EYELID_LINKED_ADDRESS,
               eyelidControlLinked ? config::EYELID_CONTROL_LINKED
                                    : config::EYELID_CONTROL_INDEPENDENT);
  EEPROM.commit();
}

bool updateBroadcastPeer(uint8_t channel) {
  if (esp_now_is_peer_exist(config::RECEIVER_MAC)) {
    esp_now_del_peer(config::RECEIVER_MAC);
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, config::RECEIVER_MAC, sizeof(config::RECEIVER_MAC));
  peerInfo.channel = channel;
  peerInfo.ifidx = WIFI_IF_STA;
  peerInfo.encrypt = false;
  return esp_now_add_peer(&peerInfo) == ESP_OK;
}

bool switchRadioChannel(uint8_t channel) {
  if (channel != config::ESPNOW_DISCOVERY_CHANNEL && !isDataChannel(channel)) {
    return false;
  }
  if (esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    return false;
  }
  if (espNowReady && !updateBroadcastPeer(channel)) {
    return false;
  }
  activeWifiChannel = channel;
  return true;
}

template <size_t Capacity>
bool sendDocument(StaticJsonDocument<Capacity> &document,
                  const char *loggedRequest = nullptr) {
  if (!espNowReady) {
    return false;
  }

  char jsonBuffer[remote_protocol::JSON_BUFFER_SIZE] = {};
  const size_t jsonLength = serializeJson(document, jsonBuffer, sizeof(jsonBuffer));
  if (jsonLength == 0 || jsonLength >= sizeof(jsonBuffer) - 1) {
    Serial.println("JSON payload is too large");
    return false;
  }

  // Send JSON bytes without a trailing NUL.
  const size_t payloadLength = jsonLength;

  const esp_err_t result = esp_now_send(
      config::RECEIVER_MAC,
      reinterpret_cast<const uint8_t *>(jsonBuffer),
      payloadLength);
  if (result != ESP_OK) {
    (void)loggedRequest;
    return false;
  }
  return true;
}

bool sendRequest(const char *request) {
  StaticJsonDocument<remote_protocol::JSON_DOCUMENT_SIZE> document;
  document["req"] = request;
  if (strcmp(request, remote_protocol::REQUEST_PAIRING) == 0) {
    document["type"] = remote_protocol::DEVICE_TYPE;
    document["name"] = remote_protocol::DEVICE_NAME;
    document["channel"] = pairingRequestedChannel;
    document["nonce"] = pairingNonce;
  }
  return sendDocument(document, request);
}

uint8_t getJoystickXToSend() {
  if (currentMode == ControlMode::AUTO_FULL_MODE) {
    return static_cast<uint8_t>(constrain(
        map(static_cast<int>(eyeState.eyeCurrentX), 60, 180, 0, 255), 0, 255));
  }

  if (joystickLocked) {
    return lockedJoystickX;
  }

  return static_cast<uint8_t>(mapCentered(
      getLogicalJoystickXRead(), 127,
      config::JOYSTICK_SWAP_XY ? config::JOYSTICK_Y_REVERSED
                               : config::JOYSTICK_X_REVERSED));
}

uint8_t getJoystickYToSend() {
  if (currentMode == ControlMode::AUTO_FULL_MODE) {
    return static_cast<uint8_t>(constrain(
        map(static_cast<int>(eyeState.eyeCurrentY), 60, 180, 0, 255), 0, 255));
  }

  if (joystickLocked) {
    return lockedJoystickY;
  }

  return static_cast<uint8_t>(mapCentered(
      getLogicalJoystickYRead(), 127,
      config::JOYSTICK_SWAP_XY ? config::JOYSTICK_X_REVERSED
                               : config::JOYSTICK_Y_REVERSED));
}

bool sendController() {
  StaticJsonDocument<remote_protocol::JSON_DOCUMENT_SIZE> document;
  document["req"] = remote_protocol::REQUEST_CONTROLLER;
  JsonObject data = document.createNestedObject("data");

  data["light"] = config::DEFAULT_LIGHT_VALUE;
  data["j1PotX"] = getJoystickXToSend();
  data["j1PotY"] = getJoystickYToSend();

  if (currentMode == ControlMode::MANUAL_MODE) {
    data["bkl"] = currentLeftBlink;
    data["bkr"] = currentRightBlink;
  } else {
    data["bkl"] = eyeState.eyeOpenness;
    data["bkr"] = eyeState.eyeOpenness;
  }

  return sendDocument(document);
}

void beginPairingSession() {
  // Pairing and control stay on the fixed channel 1. The receiver records the
  // sender MAC; there is no approval response or follow-up handshake.
  pairingRequestedChannel = config::ESPNOW_WIFI_CHANNEL;
  pairingNonce = esp_random();
  if (pairingNonce == 0) {
    pairingNonce = 1;
  }

  if (!switchRadioChannel(config::ESPNOW_WIFI_CHANNEL)) {
    Serial.println("Pairing failed: unable to switch to channel 1");
    return;
  }

  // Send the initial packet; the button gesture repeats this session while held.
  sendRequest(remote_protocol::REQUEST_PAIRING);
}

void processButtonGestures(uint32_t now,
                           bool joystickButtonPressed,
                           bool r1Pressed,
                           bool r2Pressed,
                           bool leftPressed,
                           bool rightPressed) {
  const bool allFourPressed = r1Pressed && r2Pressed && leftPressed && rightPressed;
  const bool pairingPressed = r1Pressed && r2Pressed && !leftPressed && !rightPressed;

  if (allFourPressed) {
    if (!calibrationTracking) {
      calibrationTracking = true;
      calibrationStartedAt = now;
      calibrationHandled = false;
    } else if (!calibrationHandled &&
               now - calibrationStartedAt >= config::LONG_PRESS_MS) {
      calibrateJoystick();
      calibrationHandled = true;
    }
  } else {
    calibrationTracking = false;
    calibrationHandled = false;
  }

  if (pairingPressed) {
    if (!pairingTracking) {
      pairingTracking = true;
      pairingStartedAt = now;
      lastPairingSendAt = 0;
      pairingHandled = false;
    } else if (now - pairingStartedAt >= config::LONG_PRESS_MS) {
      if (!pairingHandled) {
        pairingHandled = true;
        beginPairingSession();
        lastPairingSendAt = now;
      } else if (now - lastPairingSendAt >=
                 config::PAIRING_RETRY_INTERVAL_MS) {
        lastPairingSendAt = now;
        sendRequest(remote_protocol::REQUEST_PAIRING);
      }

    }
  } else {
    if (pairingTracking) {
      pairingTracking = false;
      pairingHandled = false;
    }
    lastPairingSendAt = 0;
  }

  if (!allFourPressed && !pairingPressed && joystickButtonPressed) {
    if (leftPressed && !lastLeftButton) {
      sendRequest(remote_protocol::REQUEST_COMBO_PREVIOUS);
      status_led::notify(status_led::Event::PREVIOUS_EYE);
    }
    if (rightPressed && !lastRightButton) {
      sendRequest(remote_protocol::REQUEST_COMBO_NEXT);
      status_led::notify(status_led::Event::NEXT_EYE);
    }
  }

  if (!allFourPressed && !pairingPressed && r1Pressed && !r2Pressed) {
    if (!r1LongPressTracking) {
      r1LongPressTracking = true;
      r1LongPressStartedAt = now;
      r1LongPressHandled = false;
    } else if (!r1LongPressHandled &&
               now - r1LongPressStartedAt >= config::LONG_PRESS_MS) {
      if (currentMode == ControlMode::MANUAL_MODE ||
          currentMode == ControlMode::AUTO_BLINK_MODE) {
        if (joystickLocked) {
          joystickLocked = false;
          status_led::notify(status_led::Event::UNLOCKED);
        } else {
          lockedJoystickX = getJoystickXToSend();
          lockedJoystickY = getJoystickYToSend();
          joystickLocked = true;
          status_led::notify(status_led::Event::LOCKED);
        }
      }
      r1LongPressHandled = true;
    }
  } else {
    r1LongPressTracking = false;
    r1LongPressHandled = false;
  }

  if (!allFourPressed && !pairingPressed && r2Pressed && !r1Pressed) {
    if (!r2LongPressTracking) {
      r2LongPressTracking = true;
      r2LongPressStartedAt = now;
      r2LongPressHandled = false;
    } else if (!r2LongPressHandled &&
               now - r2LongPressStartedAt >= config::LONG_PRESS_MS) {
      eyelidControlLinked = !eyelidControlLinked;
      if (eyelidControlLinked) {
        currentRightBlink = currentLeftBlink;
        status_led::notify(status_led::Event::LINKED_ON);
      } else {
        currentLeftBlink = currentRightBlink;
        status_led::notify(status_led::Event::LINKED_OFF);
      }
      saveEyelidControlMode();
      r2LongPressHandled = true;
    }
  } else {
    r2LongPressTracking = false;
    r2LongPressHandled = false;
  }

  if (r1Pressed && !lastR1 && !r2Pressed) {
    if (now - lastPressTimeR1 <= config::MULTI_PRESS_WINDOW_MS) {
      ++pressCountR1;
    } else {
      pressCountR1 = 1;
    }
    lastPressTimeR1 = now;

    if (pressCountR1 >= 3) {
      cycleMode();
      pressCountR1 = 0;
    }
  }
}

void commitButtonStates(bool joystickButtonPressed,
                        bool r1Pressed,
                        bool leftPressed,
                        bool rightPressed) {
  lastJoystickButton = joystickButtonPressed;
  lastR1 = r1Pressed;
  lastLeftButton = leftPressed;
  lastRightButton = rightPressed;
}

bool initializeEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  const uint8_t initialChannel = config::ESPNOW_WIFI_CHANNEL;
  if (esp_wifi_set_channel(initialChannel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    Serial.println("Wi-Fi channel setup failed");
    return false;
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW initialization failed");
    return false;
  }

  activeWifiChannel = initialChannel;
  if (!updateBroadcastPeer(initialChannel)) {
    Serial.println("Adding ESP-NOW receiver failed");
    return false;
  }

  return true;
}

void initializeInputs() {
  analogReadResolution(config::ADC_RESOLUTION_BITS);
  analogSetPinAttenuation(config::JOYSTICK_X_ADC_PIN,
                          config::JOYSTICK_ADC_ATTENUATION);
  analogSetPinAttenuation(config::JOYSTICK_Y_ADC_PIN,
                          config::JOYSTICK_ADC_ATTENUATION);
  analogSetPinAttenuation(config::BATTERY_ADC_PIN,
                          config::BATTERY_ADC_ATTENUATION);

  pinMode(config::JOYSTICK_BUTTON_PIN, INPUT_PULLUP);
  pinMode(config::LEFT_BLINK_BUTTON_PIN, INPUT_PULLUP);
  pinMode(config::RIGHT_BLINK_BUTTON_PIN, INPUT_PULLUP);
  pinMode(config::R1_BUTTON_PIN, INPUT_PULLUP);
  pinMode(config::R2_BUTTON_PIN, INPUT_PULLUP);
}

void printDiagnostics(uint32_t now,
                      bool joystickButtonPressed,
                      bool leftButtonPressed,
                      bool rightButtonPressed,
                      bool r1Pressed,
                      bool r2Pressed) {
  if (!config::ENABLE_SERIAL_DIAGNOSTICS ||
      now - lastDiagnosticsAt < config::SERIAL_DIAGNOSTICS_INTERVAL_MS) {
    return;
  }

  lastDiagnosticsAt = now;
  Serial.printf(
      "[status] mode=%s espnow=%s channel=%u joy=%d,%d raw=%u,%u "
      "batt=%.2fV/%umV rangeX=%u/%u/%u rangeY=%u/%u/%u send=%u,%u "
      "buttons=J%d LB%d RB%d R1%d R2%d linked=%d locked=%d eyelid=%.2f,%.2f\n",
      modeName(currentMode),
      espNowReady ? "ready" : "error",
      activeWifiChannel,
      joystickXRead,
      joystickYRead,
      joystickXRawSample,
      joystickYRawSample,
      static_cast<double>(batteryVoltage),
      batteryAdcMillivolts,
      joystickXRawMin,
      joystickXRawCenter,
      joystickXRawMax,
      joystickYRawMin,
      joystickYRawCenter,
      joystickYRawMax,
      getJoystickXToSend(),
      getJoystickYToSend(),
      joystickButtonPressed,
      leftButtonPressed,
      rightButtonPressed,
      r1Pressed,
      r2Pressed,
      eyelidControlLinked,
      joystickLocked,
      currentLeftBlink,
      currentRightBlink);
}

void printJoystickRawDiagnostics(uint32_t now) {
  if (!config::ENABLE_JOYSTICK_RAW_DIAGNOSTICS ||
      now - lastJoystickRawDiagnosticsAt <
          config::JOYSTICK_RAW_DIAGNOSTICS_INTERVAL_MS ||
      Serial.availableForWrite() < 96) {
    return;
  }

  lastJoystickRawDiagnosticsAt = now;
  Serial.printf("J raw=%u,%u mv=%lu,%lu filt=%u,%u norm=%d,%d out=%u,%u\n",
                joystickXRawSample,
                joystickYRawSample,
                static_cast<unsigned long>(joystickXMillivolts),
                static_cast<unsigned long>(joystickYMillivolts),
                joystickXRawFiltered,
                joystickYRawFiltered,
                joystickXRead,
                joystickYRead,
                getJoystickXToSend(),
                getJoystickYToSend());
}

}  // namespace

void setup() {
  Serial.begin(config::SERIAL_BAUD_RATE);
  const uint32_t serialWaitStartedAt = millis();
  while (!Serial && millis() - serialWaitStartedAt < 1500) {
    delay(10);
  }

  Serial.println();
  Serial.println("Draeyes RCs1 starting");

  initializeInputs();
  updateBatteryMeasurement(millis(), true);
  initializeCalibration();
  randomSeed(static_cast<uint32_t>(analogRead(config::JOYSTICK_X_ADC_PIN)) ^ esp_random());
  eye_animation::init(&eyeState);
  status_led::init();
  status_led::notify(status_led::Event::STARTUP);

  espNowReady = config::JOYSTICK_DIAGNOSTIC_ONLY ? false : initializeEspNow();
  if (config::JOYSTICK_DIAGNOSTIC_ONLY) {
    Serial.println("ADC diagnostic-only mode: ESP-NOW disabled");
  }
  if (!espNowReady) {
    Serial.println("ESP-NOW is not ready; input processing remains enabled");
  } else {
    Serial.printf("ESP-NOW ready: channel=%u mac=%s\n",
                  activeWifiChannel,
                  WiFi.macAddress().c_str());
  }
}

void loop() {
  readJoystickData();
  const uint32_t now = millis();
  updateBatteryMeasurement(now);
  updateJoystickCalibrationBounds(now);
  const bool joystickButtonPressed = isPressed(config::JOYSTICK_BUTTON_PIN);
  const bool r1Pressed = isPressed(config::R1_BUTTON_PIN);
  const bool r2Pressed = isPressed(config::R2_BUTTON_PIN);
  const bool leftButtonPressed = isPressed(config::LEFT_BLINK_BUTTON_PIN);
  const bool rightButtonPressed = isPressed(config::RIGHT_BLINK_BUTTON_PIN);
  const bool leftPressed = leftButtonPressed;
  const bool rightPressed = rightButtonPressed;

  processButtonGestures(now,
                        joystickButtonPressed,
                        r1Pressed,
                        r2Pressed,
                        leftPressed,
                        rightPressed);
  advanceManualBlinkEffects(leftPressed, rightPressed, joystickButtonPressed);
  updateAnimation();
  commitButtonStates(joystickButtonPressed,
                     r1Pressed,
                     leftPressed,
                     rightPressed);
  printDiagnostics(now,
                   joystickButtonPressed,
                   leftButtonPressed,
                   rightButtonPressed,
                   r1Pressed,
                   r2Pressed);
  printJoystickRawDiagnostics(now);
  status_led::PairingState pairingState =
      status_led::PairingState::INACTIVE;
  if (pairingTracking) {
    pairingState = now - pairingStartedAt >= config::LONG_PRESS_MS
                       ? status_led::PairingState::ACTIVE
                       : status_led::PairingState::PENDING;
  }
  status_led::update(now, batteryVoltage, pairingState, espNowReady);

  if (!config::JOYSTICK_DIAGNOSTIC_ONLY &&
      now - lastSendAt >= config::ESPNOW_SEND_INTERVAL_MS) {
    lastSendAt = now;
    sendController();
  }
}

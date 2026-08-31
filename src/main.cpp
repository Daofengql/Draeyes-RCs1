#include <Arduino.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "config.h"
#include "eye_animation.h"
#include "remote_protocol.h"

namespace {

enum class ControlMode : uint8_t {
  MANUAL_MODE,
  AUTO_FULL_MODE,
  AUTO_BLINK_MODE,
  AUTO_BLINK_LOCKED_MODE,
};

ControlMode currentMode = ControlMode::MANUAL_MODE;
bool joystickLocked = false;

int joystickXRead = 127;
int joystickYRead = 127;
uint8_t joystickXCenter = 127;
uint8_t joystickYCenter = 127;

bool lastR1 = false;
bool lastR2 = false;
bool lastLeftBlink = false;
bool lastRightBlink = false;

uint32_t lastPressTimeR1 = 0;
uint32_t lastPressTimeR2 = 0;
uint8_t pressCountR1 = 0;
uint8_t pressCountR2 = 0;

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
volatile bool pairingTransportStatusPending = false;

float leftBlinkValues[8] = {};
float rightBlinkValues[8] = {};
uint8_t leftBlinkCount = 0;
uint8_t rightBlinkCount = 0;
uint8_t leftBlinkIndex = 0;
uint8_t rightBlinkIndex = 0;
bool leftBlinkActive = false;
bool rightBlinkActive = false;
float currentLeftBlink = 1.0f;
float currentRightBlink = 1.0f;

eye_animation::EyeState eyeState = {};
uint32_t lastSendAt = 0;
uint32_t lastDiagnosticsAt = 0;
bool espNowReady = false;
bool eepromReady = false;

bool isPressed(uint8_t pin) {
  return digitalRead(pin) == LOW;
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

void readJoystickData() {
  const uint16_t xRaw = readAxisAverage(config::JOYSTICK_X_ADC_PIN);
  const uint16_t yRaw = readAxisAverage(config::JOYSTICK_Y_ADC_PIN);

  joystickXRead = constrain(
      map(xRaw, 0, config::ADC_MAX_READING, 255, 0), 0, 255);
  joystickYRead = constrain(
      map(yRaw, 0, config::ADC_MAX_READING, 255, 0), 0, 255);
}

int mapCentered(int value, int center, bool reversed) {
  center = constrain(center, 1, 254);
  value = constrain(value, 0, 255);

  int mapped = value < center
                   ? map(value, 0, center, 0, 127)
                   : map(value, center, 255, 127, 255);
  mapped = constrain(mapped, 0, 255);
  return reversed ? 255 - mapped : mapped;
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

void advanceManualBlinkEffects(bool leftPressed, bool rightPressed) {
  if (currentMode != ControlMode::MANUAL_MODE) {
    return;
  }

  if (leftPressed != lastLeftBlink) {
    startLeftBlink(leftPressed);
  }
  if (rightPressed != lastRightBlink) {
    startRightBlink(rightPressed);
  }

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
    case ControlMode::AUTO_BLINK_LOCKED_MODE:
      return "锁定+眨眼";
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
      currentMode = ControlMode::AUTO_BLINK_LOCKED_MODE;
      break;
    case ControlMode::AUTO_BLINK_LOCKED_MODE:
      currentMode = ControlMode::MANUAL_MODE;
      break;
  }

  Serial.print("模式: ");
  Serial.println(modeName(currentMode));
}

void calibrateJoystick() {
  Serial.println("joy_zero_testing...");
  readJoystickData();
  delay(300);

  joystickXCenter = static_cast<uint8_t>(constrain(joystickXRead, 1, 254));
  joystickYCenter = static_cast<uint8_t>(constrain(joystickYRead, 1, 254));

  if (eepromReady) {
    Serial.println("Writing in EEPROM...");
    EEPROM.write(config::EEPROM_X_CENTER_ADDRESS, joystickXCenter);
    EEPROM.write(config::EEPROM_Y_CENTER_ADDRESS, joystickYCenter);
    EEPROM.commit();
  }

  Serial.println("Done...");
  Serial.print(" X center: ");
  Serial.println(joystickXCenter);
  Serial.print(" Y center: ");
  Serial.println(joystickYCenter);
}

void initializeCalibration() {
  eepromReady = EEPROM.begin(config::EEPROM_SIZE);
  if (!eepromReady) {
    Serial.println("EEPROM initialization failed; using default joystick centers");
    return;
  }

  if (EEPROM.read(config::EEPROM_MAGIC_ADDRESS) != config::EEPROM_MAGIC) {
    calibrateJoystick();
    EEPROM.write(config::EEPROM_MAGIC_ADDRESS, config::EEPROM_MAGIC);
    EEPROM.commit();
  }

  joystickXCenter = static_cast<uint8_t>(constrain(
      EEPROM.read(config::EEPROM_X_CENTER_ADDRESS), 1, 254));
  joystickYCenter = static_cast<uint8_t>(constrain(
      EEPROM.read(config::EEPROM_Y_CENTER_ADDRESS), 1, 254));
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

  // Match the original STA1 packet exactly: JSON bytes without a trailing NUL.
  const size_t payloadLength = jsonLength;
  if (loggedRequest != nullptr) {
    Serial.printf(
        "[ESP-NOW] sending req=%s target=%02X:%02X:%02X:%02X:%02X:%02X "
        "channel=%u bytes=%u payload=%s\n",
        loggedRequest,
        config::RECEIVER_MAC[0],
        config::RECEIVER_MAC[1],
        config::RECEIVER_MAC[2],
        config::RECEIVER_MAC[3],
        config::RECEIVER_MAC[4],
        config::RECEIVER_MAC[5],
        config::ESPNOW_WIFI_CHANNEL,
        static_cast<unsigned int>(payloadLength),
        jsonBuffer);
  }

  const bool isPairingRequest =
      loggedRequest != nullptr &&
      strcmp(loggedRequest, remote_protocol::REQUEST_PAIRING) == 0;
  if (isPairingRequest) {
    pairingTransportStatusPending = true;
  }

  const esp_err_t result = esp_now_send(
      config::RECEIVER_MAC,
      reinterpret_cast<const uint8_t *>(jsonBuffer),
      payloadLength);
  if (result != ESP_OK) {
    if (isPairingRequest) {
      pairingTransportStatusPending = false;
    }
    Serial.printf("[ESP-NOW] queue failed: error=%d req=%s\n",
                  result,
                  loggedRequest != nullptr ? loggedRequest : "controller");
    return false;
  }
  if (loggedRequest != nullptr) {
    Serial.printf("[ESP-NOW] queued req=%s\n", loggedRequest);
  }
  return true;
}

bool sendRequest(const char *request) {
  StaticJsonDocument<remote_protocol::JSON_DOCUMENT_SIZE> document;
  document["req"] = request;
  if (strcmp(request, remote_protocol::REQUEST_PAIRING) == 0) {
    document["type"] = remote_protocol::DEVICE_TYPE;
    document["name"] = remote_protocol::DEVICE_NAME;
    document["caps"] = remote_protocol::DEVICE_CAPABILITIES;
  }
  return sendDocument(document, request);
}

uint8_t getJoystickXToSend() {
  if (currentMode == ControlMode::AUTO_FULL_MODE) {
    return static_cast<uint8_t>(constrain(
        map(static_cast<int>(eyeState.eyeCurrentX), 60, 180, 0, 255), 0, 255));
  }

  if (currentMode == ControlMode::AUTO_BLINK_LOCKED_MODE || joystickLocked) {
    return 127;
  }

  return static_cast<uint8_t>(mapCentered(
      joystickXRead, joystickXCenter, config::JOYSTICK_X_REVERSED));
}

uint8_t getJoystickYToSend() {
  if (currentMode == ControlMode::AUTO_FULL_MODE) {
    return static_cast<uint8_t>(constrain(
        map(static_cast<int>(eyeState.eyeCurrentY), 60, 180, 0, 255), 0, 255));
  }

  if (currentMode == ControlMode::AUTO_BLINK_LOCKED_MODE || joystickLocked) {
    return 127;
  }

  return static_cast<uint8_t>(mapCentered(
      joystickYRead, joystickYCenter, config::JOYSTICK_Y_REVERSED));
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

void processButtonGestures(uint32_t now,
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
      Serial.printf("[pairing] R1+R2 detected; hold for %lu ms\n",
                    static_cast<unsigned long>(config::LONG_PRESS_MS));
    } else if (now - pairingStartedAt >= config::LONG_PRESS_MS) {
      if (!pairingHandled) {
        pairingHandled = true;
        Serial.printf("[pairing] hold complete; retrying every %lu ms while held\n",
                      static_cast<unsigned long>(config::PAIRING_RETRY_INTERVAL_MS));
      }

      if (lastPairingSendAt == 0 ||
          now - lastPairingSendAt >= config::PAIRING_RETRY_INTERVAL_MS) {
        lastPairingSendAt = now;
        const bool queued = sendRequest(remote_protocol::REQUEST_PAIRING);
        Serial.println(queued ? "[pairing] request queued"
                              : "[pairing] request was not queued");
      }
    }
  } else {
    if (pairingTracking && !pairingHandled) {
      Serial.printf("[pairing] cancelled after %lu ms\n",
                    static_cast<unsigned long>(now - pairingStartedAt));
    }
    pairingTracking = false;
    pairingHandled = false;
    lastPairingSendAt = 0;
  }

  if (!allFourPressed && !pairingPressed && r1Pressed && !r2Pressed) {
    if (!r1LongPressTracking) {
      r1LongPressTracking = true;
      r1LongPressStartedAt = now;
      r1LongPressHandled = false;
    } else if (!r1LongPressHandled &&
               now - r1LongPressStartedAt >= config::LONG_PRESS_MS) {
      Serial.println("R1 long press: light sensor control is unavailable on Mini");
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
      if (currentMode == ControlMode::MANUAL_MODE ||
          currentMode == ControlMode::AUTO_BLINK_MODE) {
        joystickLocked = !joystickLocked;
        Serial.println(joystickLocked ? "R2 long press: joystick locked"
                                      : "R2 long press: joystick unlocked");
      }
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

  if (r2Pressed && !lastR2 && !r1Pressed) {
    if (now - lastPressTimeR2 <= config::MULTI_PRESS_WINDOW_MS) {
      ++pressCountR2;
    } else {
      pressCountR2 = 1;
    }
    lastPressTimeR2 = now;

    if (pressCountR2 >= 3) {
      sendRequest(remote_protocol::REQUEST_CHANGE);
      pressCountR2 = 0;
    }
  }
}

void commitButtonStates(bool r1Pressed,
                        bool r2Pressed,
                        bool leftPressed,
                        bool rightPressed) {
  lastR1 = r1Pressed;
  lastR2 = r2Pressed;
  lastLeftBlink = leftPressed;
  lastRightBlink = rightPressed;
}

void onEspNowSent(const uint8_t *, esp_now_send_status_t status) {
  if (pairingTransportStatusPending) {
    pairingTransportStatusPending = false;
    Serial.printf("[pairing] ESP-NOW transport=%s (receiver approval is separate)\n",
                  status == ESP_NOW_SEND_SUCCESS ? "success" : "failed");
    return;
  }
  if (status != ESP_NOW_SEND_SUCCESS) {
    Serial.println("ESP-NOW send failed");
  }
}

bool initializeEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_wifi_set_channel(config::ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    Serial.println("Wi-Fi channel setup failed");
    return false;
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW initialization failed");
    return false;
  }

  if (esp_now_register_send_cb(onEspNowSent) != ESP_OK) {
    Serial.println("ESP-NOW callback setup failed");
    return false;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, config::RECEIVER_MAC, sizeof(config::RECEIVER_MAC));
  peerInfo.channel = config::ESPNOW_WIFI_CHANNEL;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Adding ESP-NOW receiver failed");
    return false;
  }

  return true;
}

void initializeInputs() {
  analogReadResolution(config::ADC_RESOLUTION_BITS);

  // Kept ready for a future joystick-button action; currently ignored.
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
      "[status] mode=%s espnow=%s joy=%d,%d send=%u,%u center=%u,%u "
      "buttons=J%d LB%d RB%d R1%d R2%d eyelid=%.2f,%.2f\n",
      modeName(currentMode),
      espNowReady ? "ready" : "error",
      joystickXRead,
      joystickYRead,
      getJoystickXToSend(),
      getJoystickYToSend(),
      joystickXCenter,
      joystickYCenter,
      joystickButtonPressed,
      leftButtonPressed,
      rightButtonPressed,
      r1Pressed,
      r2Pressed,
      currentLeftBlink,
      currentRightBlink);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t serialWaitStartedAt = millis();
  while (!Serial && millis() - serialWaitStartedAt < 1500) {
    delay(10);
  }

  Serial.println();
  Serial.println("Draeye RC Mini starting");

  initializeInputs();
  initializeCalibration();
  randomSeed(static_cast<uint32_t>(analogRead(config::JOYSTICK_X_ADC_PIN)));
  eye_animation::init(&eyeState);

  espNowReady = initializeEspNow();
  if (!espNowReady) {
    Serial.println("ESP-NOW is not ready; input processing remains enabled");
  } else {
    Serial.printf("ESP-NOW ready: channel=%u mac=%s\n",
                  config::ESPNOW_WIFI_CHANNEL,
                  WiFi.macAddress().c_str());
  }
}

void loop() {
  readJoystickData();
  const uint32_t now = millis();
  const bool joystickButtonPressed = isPressed(config::JOYSTICK_BUTTON_PIN);
  const bool r1Pressed = isPressed(config::R1_BUTTON_PIN);
  const bool r2Pressed = isPressed(config::R2_BUTTON_PIN);
  const bool leftButtonPressed = isPressed(config::LEFT_BLINK_BUTTON_PIN);
  const bool rightButtonPressed = isPressed(config::RIGHT_BLINK_BUTTON_PIN);
  const bool leftPressed = leftButtonPressed;
  const bool rightPressed = rightButtonPressed;

  processButtonGestures(now, r1Pressed, r2Pressed, leftPressed, rightPressed);
  advanceManualBlinkEffects(leftPressed, rightPressed);
  updateAnimation();
  commitButtonStates(r1Pressed, r2Pressed, leftPressed, rightPressed);
  printDiagnostics(now,
                   joystickButtonPressed,
                   leftButtonPressed,
                   rightButtonPressed,
                   r1Pressed,
                   r2Pressed);

  // Do not interleave control traffic with pairing retries. This makes the
  // pairing callback unambiguous and avoids flooding the broadcast channel.
  if (!pairingTracking && now - lastSendAt >= config::ESPNOW_SEND_INTERVAL_MS) {
    lastSendAt = now;
    sendController();
  }
}

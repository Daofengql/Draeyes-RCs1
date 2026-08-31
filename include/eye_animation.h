#pragma once

#include <Arduino.h>
#include <math.h>

namespace eye_animation {

constexpr float MAP_CENTER = 120.0f;

enum BlinkState : uint8_t {
  NOT_BLINKING = 0,
  BLINK_CLOSING = 1,
  BLINK_OPENING = 2,
};

struct EyeState {
  float eyeCurrentX;
  float eyeCurrentY;
  float eyeOldX;
  float eyeOldY;
  float eyeNewX;
  float eyeNewY;

  bool inMotion;
  uint32_t moveStartTimeMs;
  uint32_t moveDurationMs;
  uint32_t lastSaccadeStopMs;
  uint32_t saccadeIntervalMs;

  BlinkState blinkState;
  uint32_t blinkStartTimeMs;
  uint32_t blinkDurationMs;
  float blinkFactor;
  uint32_t timeOfLastBlinkMs;
  uint32_t timeToNextBlinkMs;

  float eyeOpenness;
};

inline float randomFloat(float minimum, float maximum) {
  if (minimum >= maximum) {
    return minimum;
  }

  const float scale = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
  return minimum + scale * (maximum - minimum);
}

inline void init(EyeState *state) {
  state->eyeOldX = state->eyeNewX = MAP_CENTER;
  state->eyeOldY = state->eyeNewY = MAP_CENTER;
  state->eyeCurrentX = MAP_CENTER;
  state->eyeCurrentY = MAP_CENTER;

  state->inMotion = false;
  state->moveDurationMs = 0;
  state->moveStartTimeMs = millis();
  state->lastSaccadeStopMs = state->moveStartTimeMs;
  state->saccadeIntervalMs = random(500, 3001);

  state->blinkState = NOT_BLINKING;
  state->blinkStartTimeMs = 0;
  state->blinkDurationMs = 0;
  state->blinkFactor = 0.0f;
  state->timeOfLastBlinkMs = millis();
  state->timeToNextBlinkMs = random(2000, 6001);
  state->eyeOpenness = 1.0f;
}

inline void triggerBlink(EyeState *state) {
  if (state->blinkState == NOT_BLINKING) {
    state->blinkState = BLINK_CLOSING;
    state->blinkStartTimeMs = millis();
    state->blinkDurationMs = random(50, 101);
  }
}

inline void updateAutoMovement(EyeState *state, uint32_t maxGazeMs) {
  const uint32_t now = millis();
  const uint32_t elapsed = now - state->moveStartTimeMs;

  if (state->inMotion) {
    if (elapsed >= state->moveDurationMs) {
      state->inMotion = false;
      state->eyeOldX = state->eyeNewX;
      state->eyeOldY = state->eyeNewY;
      state->eyeCurrentX = state->eyeNewX;
      state->eyeCurrentY = state->eyeNewY;

      const uint32_t limit = maxGazeMs < 1000 ? maxGazeMs : 1000;
      state->moveDurationMs = random(35, static_cast<long>(limit + 1));
      if (state->saccadeIntervalMs == 0) {
        state->lastSaccadeStopMs = now;
        state->saccadeIntervalMs = random(
            static_cast<long>(state->moveDurationMs), static_cast<long>(maxGazeMs + 1));
      }
      state->moveStartTimeMs = now;
    } else if (state->moveDurationMs != 0) {
      float progress = static_cast<float>(elapsed) /
                       static_cast<float>(state->moveDurationMs);
      const float progressSquared = progress * progress;
      progress = progressSquared * (3.0f - 2.0f * progress);
      state->eyeCurrentX = state->eyeOldX +
                           (state->eyeNewX - state->eyeOldX) * progress;
      state->eyeCurrentY = state->eyeOldY +
                           (state->eyeNewY - state->eyeOldY) * progress;
    }
    return;
  }

  if (elapsed <= state->moveDurationMs) {
    return;
  }

  if (now - state->lastSaccadeStopMs > state->saccadeIntervalMs) {
    constexpr float maxRadius = 60.0f;
    const float relativeX = randomFloat(-maxRadius, maxRadius);
    const float maxY = sqrtf(maxRadius * maxRadius - relativeX * relativeX);
    const float relativeY = randomFloat(-maxY, maxY);

    state->eyeNewX = constrain(MAP_CENTER + relativeX, 60.0f, 180.0f);
    state->eyeNewY = constrain(MAP_CENTER + relativeY, 60.0f, 180.0f);
    state->moveDurationMs = random(83, 167);
    state->saccadeIntervalMs = 0;
  } else {
    constexpr float microRadius = 8.0f;
    const float dx = randomFloat(-microRadius, microRadius);
    const float maxDy = sqrtf(microRadius * microRadius - dx * dx);
    const float dy = randomFloat(-maxDy, maxDy);

    state->eyeNewX = constrain(state->eyeOldX + dx, 60.0f, 180.0f);
    state->eyeNewY = constrain(state->eyeOldY + dy, 60.0f, 180.0f);
    state->moveDurationMs = random(7, 26);
  }

  state->moveStartTimeMs = now;
  state->inMotion = true;
}

inline void updateAutoBlink(EyeState *state) {
  const uint32_t now = millis();
  if (now - state->timeOfLastBlinkMs >= state->timeToNextBlinkMs) {
    state->timeOfLastBlinkMs = now;
    triggerBlink(state);
    state->timeToNextBlinkMs = state->blinkDurationMs * 3 + random(2000, 6001);
  }
}

inline void updateBlinkState(EyeState *state) {
  const uint32_t now = millis();

  if (state->blinkState != NOT_BLINKING) {
    if (now - state->blinkStartTimeMs >= state->blinkDurationMs) {
      switch (state->blinkState) {
        case BLINK_CLOSING:
          state->blinkState = BLINK_OPENING;
          state->blinkDurationMs *= 2;
          state->blinkStartTimeMs = now;
          state->blinkFactor = 1.0f;
          break;
        case BLINK_OPENING:
          state->blinkState = NOT_BLINKING;
          state->blinkFactor = 0.0f;
          break;
        case NOT_BLINKING:
          break;
      }
    } else if (state->blinkDurationMs != 0) {
      const float progress = static_cast<float>(now - state->blinkStartTimeMs) /
                             static_cast<float>(state->blinkDurationMs);
      state->blinkFactor = state->blinkState == BLINK_CLOSING
                               ? progress
                               : 1.0f - progress;
    }
  }

  state->eyeOpenness = 1.0f - state->blinkFactor;
}

}  // namespace eye_animation

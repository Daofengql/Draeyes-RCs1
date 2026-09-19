#pragma once

#include <stdint.h>

namespace remote_protocol {

constexpr uint16_t JSON_DOCUMENT_SIZE = 256;
constexpr uint16_t JSON_BUFFER_SIZE = 256;

constexpr const char *REQUEST_CONTROLLER = "controller";
constexpr const char *REQUEST_PAIRING = "pairing";
constexpr const char *REQUEST_COMBO_PREVIOUS = "cp";
constexpr const char *REQUEST_COMBO_NEXT = "cs";

constexpr const char *DEVICE_TYPE = "remote";
constexpr const char *DEVICE_NAME = "Draeye RC Mini";

}  // namespace remote_protocol

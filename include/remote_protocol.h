#pragma once

#include <stdint.h>

namespace remote_protocol {

constexpr uint16_t JSON_DOCUMENT_SIZE = 256;
constexpr uint16_t JSON_BUFFER_SIZE = 256;

constexpr const char *REQUEST_CONTROLLER = "controller";
constexpr const char *REQUEST_PAIRING = "pairing";
constexpr const char *REQUEST_CHANGE = "change";

// Optional metadata consumed by the S3 pairing page. Legacy C3 receivers
// only inspect "req" and ignore these extra fields.
constexpr const char *DEVICE_TYPE = "remote";
constexpr const char *DEVICE_NAME = "Draeye RC Mini";
constexpr uint32_t DEVICE_CAPABILITIES = 0;

}  // namespace remote_protocol

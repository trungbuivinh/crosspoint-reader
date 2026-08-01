#pragma once

#include <cstddef>
#include <cstdint>

namespace HttpRequestDiagnostics {

enum class FailureStage : uint8_t {
  NONE,
  CLIENT_INIT,
  OOM,
  OPEN,
  HEADERS,
  REDIRECT,
  HTTP_STATUS,
  READ,
  INCOMPLETE,
  SINK,
};

/**
 * Bounded terminal diagnostics for a single HTTP request. The caller owns the
 * storage, so capturing a server error never allocates on the X4 heap.
 */
struct FailureDetails {
  static constexpr size_t SERVER_MESSAGE_BYTES = 192;

  FailureStage stage = FailureStage::NONE;
  int httpStatus = 0;
  int transportError = 0;
  size_t receivedBytes = 0;
  char serverMessage[SERVER_MESSAGE_BYTES]{};
};

inline const char* failureStageName(const FailureStage stage) {
  switch (stage) {
    case FailureStage::NONE:
      return "none";
    case FailureStage::CLIENT_INIT:
      return "client-init";
    case FailureStage::OOM:
      return "oom";
    case FailureStage::OPEN:
      return "open";
    case FailureStage::HEADERS:
      return "headers";
    case FailureStage::REDIRECT:
      return "redirect";
    case FailureStage::HTTP_STATUS:
      return "http-status";
    case FailureStage::READ:
      return "read";
    case FailureStage::INCOMPLETE:
      return "incomplete";
    case FailureStage::SINK:
      return "sink";
  }
  return "unknown";
}

}  // namespace HttpRequestDiagnostics

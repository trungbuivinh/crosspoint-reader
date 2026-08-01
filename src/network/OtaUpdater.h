#pragma once

#include <atomic>
#include <string>

#include "OtaRelease.h"

class OtaUpdater {
  bool updateAvailable = false;
  std::string latestVersion;
  std::string otaUrl;
  size_t otaSize = 0;
  std::atomic_size_t processedSize{0};
  size_t totalSize = 0;

 public:
  using ProgressCallback = void (*)(void* ctx);

  enum OtaUpdaterError {
    OK = 0,
    NO_UPDATE,
    HTTP_ERROR,
    JSON_PARSE_ERROR,
    UPDATE_OLDER_ERROR,
    INTERNAL_UPDATE_ERROR,
    OOM_ERROR,
  };

  size_t getOtaSize() const { return otaSize; }

  size_t getProcessedSize() const { return processedSize.load(std::memory_order_relaxed); }

  size_t getTotalSize() const { return totalSize; }

  OtaUpdater() = default;
  bool isUpdateNewer() const;
  const std::string& getLatestVersion() const;
  OtaUpdaterError checkForUpdate(OtaUpdateSource source);
  OtaUpdaterError installUpdate(ProgressCallback onProgress = nullptr, void* ctx = nullptr);
};

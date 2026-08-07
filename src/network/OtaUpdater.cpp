#include "OtaUpdater.h"

// clang-format off
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's
// ip4_addr.h unless seen first. Pin this order; clang-format would otherwise sort
// the local header last and break the build.
#include "HttpDownloader.h"
#include "OtaRelease.h"
#include "OtaVersion.h"
#include <Logging.h>
#include <Memory.h>
#include <ReleaseJsonParser.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
// clang-format on

#include <algorithm>
#include <cstring>
#include <string>

#include "FirmwareFlasher.h"

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate(const OtaUpdateSource source) {
  updateAvailable = false;
  latestVersion.clear();
  otaUrl.clear();
  otaSize = 0;
  processedSize.store(0, std::memory_order_relaxed);
  totalSize = 0;

  const char* latestReleaseUrl = OtaRelease::latestReleaseUrl(source);
  LOG_DBG("OTA", "Checking %s source for update (current: %s)",
          source == OtaUpdateSource::Official ? "official" : "custom", CROSSPOINT_VERSION);

  // Stream the ~32KB release JSON straight into the parser as it arrives.
  // Buffering the whole body in a std::string would add a growing allocation
  // on top of the TLS session's heap during the fetch; with -fno-exceptions an
  // OOM there aborts. fetchUrl handles the verified-https GET, redirects, and
  // User-Agent (see HttpDownloader).
  auto releaseParser = makeUniqueNoThrow<ReleaseJsonParser>();
  if (!releaseParser) {
    LOG_ERR("OTA", "OOM allocating release response parser");
    return OOM_ERROR;
  }
  const bool ok = HttpDownloader::fetchUrl(latestReleaseUrl, [&releaseParser](const uint8_t* data, size_t len) {
    releaseParser->feed(reinterpret_cast<const char*>(data), len);
    return true;
  });
  if (!ok) {
    LOG_ERR("OTA", "Release check fetch failed");
    return HTTP_ERROR;
  }
  if (!releaseParser->finish()) {
    LOG_ERR("OTA", "Incomplete or malformed release JSON");
    return JSON_PARSE_ERROR;
  }

  LOG_DBG("OTA", "Parser results: tag=%s firmware=%s", releaseParser->foundTag() ? "yes" : "no",
          releaseParser->foundFirmware() ? "yes" : "no");

  if (!releaseParser->foundTag()) {
    LOG_ERR("OTA", "No tag_name in release JSON");
    return JSON_PARSE_ERROR;
  }

  if (!releaseParser->foundFirmware()) {
    LOG_ERR("OTA", "Missing or empty firmware.bin asset");
    return JSON_PARSE_ERROR;
  }

  if (OtaVersion::compare(CROSSPOINT_VERSION, releaseParser->getTagName()) == OtaVersion::Comparison::Invalid) {
    LOG_ERR("OTA", "Malformed release tag: %s", releaseParser->getTagName());
    return JSON_PARSE_ERROR;
  }

  if (!OtaRelease::isExpectedFirmwareUrl(source, releaseParser->getFirmwareUrl(), releaseParser->getTagName())) {
    LOG_ERR("OTA", "Firmware asset URL does not match the selected source");
    return JSON_PARSE_ERROR;
  }

  latestVersion = releaseParser->getTagName();
  otaUrl = releaseParser->getFirmwareUrl();
  otaSize = releaseParser->getFirmwareSize();
  totalSize = otaSize;
  updateAvailable = true;

  LOG_DBG("OTA", "Found update: tag=%s size=%zu", latestVersion.c_str(), otaSize);
  LOG_DBG("OTA", "Firmware URL: %s", otaUrl.c_str());
  return OK;
}

bool OtaUpdater::isUpdateNewer() const {
  return updateAvailable &&
         OtaVersion::compare(CROSSPOINT_VERSION, latestVersion.c_str()) == OtaVersion::Comparison::Newer;
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback onProgress, void* ctx) {
  if (!isUpdateNewer()) {
    return UPDATE_OLDER_ERROR;
  }

  // esp_https_ota is hardwired to esp-tls/mbedTLS, whose precompiled build on this
  // package can't negotiate TLS 1.3 (see SecureClient.h). Drive the OTA partition
  // ourselves and stream the firmware through HttpDownloader, which runs over
  // wolfSSL when FREEINK_NET_WOLFSSL is set, reusing its redirect handling for the
  // GitHub -> CDN hop.
  const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
  if (!updatePartition) {
    LOG_ERR("OTA", "No OTA partition available");
    return INTERNAL_UPDATE_ERROR;
  }
  if (otaSize == 0 || otaSize > updatePartition->size) {
    LOG_ERR("OTA", "Firmware size %zu exceeds OTA partition %zu", otaSize, static_cast<size_t>(updatePartition->size));
    return INTERNAL_UPDATE_ERROR;
  }

  esp_ota_handle_t otaHandle = 0;
  esp_err_t esp_err = esp_ota_begin(updatePartition, otaSize, &otaHandle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_begin failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  /* For better timing and connectivity, we disable power saving for WiFi */
  esp_wifi_set_ps(WIFI_PS_NONE);

  processedSize.store(0, std::memory_order_relaxed);
  int lastReportedPct = -1;
  bool flashOk = true;
  // The image streams in chunks; only the first bytes carry the header. Buffer
  // the first 14 bytes so we can read chip_id (esp_image_header_t offset 12)
  // and reject a wrong-MCU image before it overwrites the OTA partition.
  uint8_t hdr[14];
  size_t hdrLen = 0;
  bool wrongChip = false;
  bool sizeMismatch = false;
  const bool fetchOk = HttpDownloader::fetchUrl(otaUrl, [&](const uint8_t* data, size_t len) {
    if (hdrLen < sizeof(hdr)) {
      const size_t take = std::min(len, sizeof(hdr) - hdrLen);
      std::memcpy(hdr + hdrLen, data, take);
      hdrLen += take;
      if (hdrLen == sizeof(hdr)) {
        uint16_t imageChip;
        std::memcpy(&imageChip, hdr + 12, sizeof(imageChip));
        const uint16_t deviceChip = firmware_flash::runningPartitionChipId();
        if (deviceChip != 0xFFFF && imageChip != deviceChip) {
          LOG_ERR("OTA", "wrong chip: image=0x%04X device=0x%04X", imageChip, deviceChip);
          wrongChip = true;
          return false;  // abort the transfer
        }
      }
    }
    const size_t currentSize = processedSize.load(std::memory_order_relaxed);
    if (currentSize > otaSize || len > otaSize - currentSize) {
      sizeMismatch = true;
      return false;
    }
    if (esp_ota_write(otaHandle, data, len) != ESP_OK) {
      flashOk = false;
      return false;  // abort the transfer
    }
    const size_t downloadedSize = processedSize.fetch_add(len, std::memory_order_relaxed) + len;
    // Fire the callback only on whole-percent change. Per-chunk updates wake the
    // render task, whose framebuffer work contends with TLS on the internal arena,
    // and e-ink can't repaint faster than a percent tick anyway.
    if (onProgress && totalSize > 0) {
      const int pct = static_cast<int>(static_cast<uint64_t>(downloadedSize) * 100 / totalSize);
      if (pct != lastReportedPct) {
        lastReportedPct = pct;
        onProgress(ctx);
      }
    }
    return true;
  });

  /* Return back to default power saving for WiFi in case of failing */
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  if (wrongChip) {
    LOG_ERR("OTA", "Firmware install aborted: wrong device");
    esp_ota_abort(otaHandle);
    return WRONG_DEVICE_ERROR;
  }

  if (sizeMismatch) {
    LOG_ERR("OTA", "Firmware exceeded advertised size of %zu bytes", otaSize);
    esp_ota_abort(otaHandle);
    return INTERNAL_UPDATE_ERROR;
  }

  if (!fetchOk || !flashOk) {
    LOG_ERR("OTA", "Firmware install failed (%s)", flashOk ? "download" : "flash write");
    esp_ota_abort(otaHandle);
    return flashOk ? HTTP_ERROR : INTERNAL_UPDATE_ERROR;
  }

  const size_t downloadedSize = processedSize.load(std::memory_order_relaxed);
  if (downloadedSize != otaSize) {
    LOG_ERR("OTA", "Firmware size mismatch: API=%zu downloaded=%zu", otaSize, downloadedSize);
    esp_ota_abort(otaHandle);
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_ota_end(otaHandle);  // verifies the written image
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_end failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_ota_set_boot_partition(updatePartition);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_set_boot_partition failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_INF("OTA", "Update completed");
  return OK;
}

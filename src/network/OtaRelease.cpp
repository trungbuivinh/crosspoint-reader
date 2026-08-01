#include "OtaRelease.h"

#include <string_view>

namespace {

constexpr char OFFICIAL_LATEST_RELEASE_URL[] =
    "https://api.github.com/repos/crosspoint-reader/crosspoint-reader/releases/latest";
constexpr char CUSTOM_LATEST_RELEASE_URL[] =
    "https://api.github.com/repos/trungbuivinh/crosspoint-reader/releases/latest";
constexpr char OFFICIAL_ASSET_PREFIX[] =
    "https://github.com/crosspoint-reader/crosspoint-reader/releases/download/";
constexpr char CUSTOM_ASSET_PREFIX[] = "https://github.com/trungbuivinh/crosspoint-reader/releases/download/";

}  // namespace

namespace OtaRelease {

const char* latestReleaseUrl(const OtaUpdateSource source) {
  return source == OtaUpdateSource::Official ? OFFICIAL_LATEST_RELEASE_URL : CUSTOM_LATEST_RELEASE_URL;
}

bool isExpectedFirmwareUrl(const OtaUpdateSource source, const char* url, const char* tag) {
  if (!url || !tag || tag[0] == '\0') return false;

  const std::string_view value(url);
  const std::string_view prefix(source == OtaUpdateSource::Official ? OFFICIAL_ASSET_PREFIX : CUSTOM_ASSET_PREFIX);
  const std::string_view version(tag);
  constexpr std::string_view suffix("/firmware.bin");

  if (!value.starts_with(prefix)) return false;
  const std::string_view path = value.substr(prefix.size());
  return path.size() == version.size() + suffix.size() && path.starts_with(version) && path.ends_with(suffix);
}

}  // namespace OtaRelease

#include <gtest/gtest.h>

#include "network/OtaRelease.h"

TEST(OtaRelease, MapsSourcesToFixedLatestReleaseEndpoints) {
  EXPECT_STREQ(OtaRelease::latestReleaseUrl(OtaUpdateSource::Official),
               "https://api.github.com/repos/crosspoint-reader/crosspoint-reader/releases/latest");
  EXPECT_STREQ(OtaRelease::latestReleaseUrl(OtaUpdateSource::Custom),
               "https://api.github.com/repos/trungbuivinh/crosspoint-reader/releases/latest");
}

TEST(OtaRelease, AcceptsOnlyExactSelectedRepositoryAsset) {
  EXPECT_TRUE(OtaRelease::isExpectedFirmwareUrl(
      OtaUpdateSource::Official,
      "https://github.com/crosspoint-reader/crosspoint-reader/releases/download/v1.5.0/firmware.bin", "v1.5.0"));
  EXPECT_TRUE(OtaRelease::isExpectedFirmwareUrl(
      OtaUpdateSource::Custom,
      "https://github.com/trungbuivinh/crosspoint-reader/releases/download/1.5.0.1/firmware.bin", "1.5.0.1"));

  EXPECT_FALSE(OtaRelease::isExpectedFirmwareUrl(
      OtaUpdateSource::Custom,
      "https://github.com/crosspoint-reader/crosspoint-reader/releases/download/1.5.0.1/firmware.bin", "1.5.0.1"));
  EXPECT_FALSE(OtaRelease::isExpectedFirmwareUrl(
      OtaUpdateSource::Official,
      "https://github.com/crosspoint-reader/crosspoint-reader/releases/download/v1.5.0/firmware.bin?asset=other",
      "v1.5.0"));
  EXPECT_FALSE(OtaRelease::isExpectedFirmwareUrl(
      OtaUpdateSource::Official,
      "https://github.com/crosspoint-reader/crosspoint-reader/releases/download/v1.5.1/firmware.bin", "v1.5.0"));
  EXPECT_FALSE(OtaRelease::isExpectedFirmwareUrl(OtaUpdateSource::Official, nullptr, "v1.5.0"));
}

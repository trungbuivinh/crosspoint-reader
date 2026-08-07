#pragma once

enum class OtaUpdateSource { Official, Custom };

namespace OtaRelease {

/** GitHub REST endpoint used to discover the selected stable release. */
const char* latestReleaseUrl(OtaUpdateSource source);

/**
 * Accept only the exact firmware.bin browser-download URL for the selected
 * repository and release tag. Redirect destinations are verified separately
 * by the TLS transport when the asset is downloaded.
 */
bool isExpectedFirmwareUrl(OtaUpdateSource source, const char* url, const char* tag);

}  // namespace OtaRelease

#!/usr/bin/env bash

set -euo pipefail

failures=0

pass() {
  printf 'PASS: %s\n' "$1"
}

fail() {
  printf 'FAIL: %s\n' "$1" >&2
  failures=$((failures + 1))
}

require_file() {
  if [[ -f "$1" ]]; then
    pass "file exists: $1"
  else
    fail "missing required file: $1"
  fi
}

require_text() {
  local text="$1"
  local file="$2"
  local label="$3"
  if [[ -f "${file}" ]] && grep -Fq -- "${text}" "${file}"; then
    pass "${label}"
  else
    fail "${label}"
  fi
}

reject_text() {
  local text="$1"
  local file="$2"
  local label="$3"
  if [[ -f "${file}" ]] && grep -Fq -- "${text}" "${file}"; then
    fail "${label}"
  else
    pass "${label}"
  fi
}

require_order() {
  local first="$1"
  local second="$2"
  local file="$3"
  local label="$4"
  local first_line=""
  local second_line=""

  if [[ -f "${file}" ]]; then
    first_line="$(awk -v needle="${first}" 'index($0, needle) { print NR; exit }' "${file}")"
    second_line="$(awk -v needle="${second}" 'index($0, needle) { print NR; exit }' "${file}")"
  fi

  if [[ -n "${first_line}" && -n "${second_line}" && "${first_line}" -lt "${second_line}" ]]; then
    pass "${label}"
  else
    fail "${label}"
  fi
}

if ! git rev-parse --show-toplevel >/dev/null 2>&1; then
  printf 'Run this audit inside the CrossPoint Reader repository.\n' >&2
  exit 2
fi

repo_root="$(git rev-parse --show-toplevel)"
cd "${repo_root}"

expected_version="${1:-}"
config_version="$(awk '
  /^\[crosspoint\][[:space:]]*$/ { in_section = 1; next }
  /^\[/ { in_section = 0 }
  in_section && /^[[:space:]]*version[[:space:]]*=/ {
    sub(/^[^=]*=[[:space:]]*/, "")
    sub(/[[:space:]]*$/, "")
    print
    exit
  }
' platformio.ini)"

if [[ -z "${config_version}" ]]; then
  fail "platformio.ini has a readable [crosspoint] version"
else
  pass "platformio.ini version is ${config_version}"
fi

if [[ -n "${expected_version}" && "${config_version}" != "${expected_version}" ]]; then
  fail "expected version ${expected_version} matches platformio.ini (${config_version})"
elif [[ -n "${expected_version}" ]]; then
  pass "expected version matches platformio.ini"
fi

if [[ "${config_version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  pass "fork version has four numeric components"
else
  fail "fork version is an exact four-component numeric version"
fi

branch="$(git branch --show-current)"
if [[ "${branch}" == release/* ]]; then
  if [[ "${branch#release/}" == "${config_version}" ]]; then
    pass "release branch matches firmware version"
  else
    fail "release branch ${branch} does not match firmware version ${config_version}"
  fi
else
  pass "working branch is ${branch:-detached}; release-branch equality deferred"
fi

origin_url="$(git remote get-url origin 2>/dev/null || true)"
upstream_url="$(git remote get-url upstream 2>/dev/null || true)"
if [[ "${origin_url}" =~ github\.com[:/]trungbuivinh/crosspoint-reader(\.git)?$ ]]; then
  pass "origin points to the personal fork"
else
  fail "origin does not point to trungbuivinh/crosspoint-reader: ${origin_url:-missing}"
fi
if [[ "${upstream_url}" =~ github\.com[:/]crosspoint-reader/crosspoint-reader(\.git)?$ ]]; then
  pass "upstream points to the original repository"
else
  fail "upstream does not point to crosspoint-reader/crosspoint-reader: ${upstream_url:-missing}"
fi

# Google Drive exact-mirror surface and allocation order.
require_file src/activities/network/GoogleDriveSyncActivity.cpp
require_file src/activities/network/NetworkModeSelectionActivity.cpp
require_file src/components/icons/gdrive.h
require_file src/GoogleDriveStore.cpp
require_file src/network/GoogleDriveClient.cpp
require_file src/network/GoogleDriveSyncPlan.cpp
require_file lib/JsonParser/DriveListJsonParser.h
require_file docs/google-drive-sync.md
require_file test/google_drive_sync_plan/GoogleDriveSyncPlanTest.cpp
require_text 'GOOGLE_DRIVE' src/activities/network/NetworkModeSelectionActivity.cpp \
  'Google Drive remains a File Transfer mode'
require_text 'UIIcon::GoogleDrive' src/activities/network/NetworkModeSelectionActivity.cpp \
  'Google Drive retains its dedicated menu icon'
require_text '"gdriveLocalFolder"' src/SettingsList.h \
  'Google Drive book-folder setting remains exposed'
require_text 'constexpr size_t MAX_TREE_ENTRIES = 300' src/network/GoogleDriveClient.h \
  'Drive tree remains explicitly bounded'
require_text 'constexpr size_t INITIAL_REMOTE_TREE_RESERVE = 64' src/network/GoogleDriveClient.cpp \
  'remote Drive tree keeps a small TLS-time reserve'
require_text 'constexpr size_t REMOTE_TREE_RESERVE_STEP = 64' src/network/GoogleDriveClient.cpp \
  'remote Drive capacity grows in bounded steps'
require_text 'releaseTreeStorageBeforeListing();' src/activities/network/GoogleDriveSyncActivity.cpp \
  'retained tree capacity is released before listing retries'
require_order 'releaseTreeStorageBeforeListing();' 'GoogleDriveClient::listTree(' \
  src/activities/network/GoogleDriveSyncActivity.cpp \
  'retained tree capacity is released before remote HTTPS listing'
require_order 'GoogleDriveClient::listTree(' 'localTree.reserve(GoogleDriveClient::MAX_TREE_ENTRIES)' \
  src/activities/network/GoogleDriveSyncActivity.cpp \
  'local Drive capacity is allocated after remote HTTPS listing'
require_order 'const bool ok = HttpDownloader::fetchUrl' 'out.nodes.reserve(std::min' \
  src/network/GoogleDriveClient.cpp \
  'remote Drive capacity grows only after each HTTPS request returns'
require_text 'makeUniqueNoThrow<DriveListJsonParser>' src/network/GoogleDriveClient.cpp \
  'Drive response parser uses fallible heap allocation'
require_text 'bool finish() { return parser.finish(); }' lib/JsonParser/DriveListJsonParser.h \
  'Drive JSON exposes fail-closed finalization'
require_text 'PART_SUFFIX[] = ".gdrive.part"' src/activities/network/GoogleDriveSyncActivity.cpp \
  'Drive downloads retain a temporary-file boundary'
require_text 'file.md5Checksum' src/activities/network/GoogleDriveSyncActivity.cpp \
  'Drive downloads retain checksum validation'
require_text 'RenderLock lock(*this)' src/activities/network/GoogleDriveSyncActivity.cpp \
  'Drive render-visible state uses RenderLock'
require_text 'if (sink.progress) sink.progress(sink.downloaded, sink.total);' src/network/HttpDownloader.cpp \
  'unknown-length downloads continue polling progress and cancellation'

# OTA source, metadata, partition, and rollback safety.
require_file src/activities/settings/OtaUpdateActivity.cpp
require_file src/network/OtaRelease.cpp
require_file src/network/OtaVersion.cpp
require_file test/ota_release/OtaReleaseTest.cpp
require_file test/ota_version/OtaVersionTest.cpp
require_text 'selectedSourceIndex = 1;' src/activities/settings/OtaUpdateActivity.cpp \
  'Custom OTA source is selected by default'
require_text 'updateSource = OtaUpdateSource::Custom;' src/activities/settings/OtaUpdateActivity.cpp \
  'Custom OTA source is the default behavior'
require_text 'crosspoint-reader/crosspoint-reader/releases/latest' src/network/OtaRelease.cpp \
  'Official OTA endpoint is fixed to upstream'
require_text 'trungbuivinh/crosspoint-reader/releases/latest' src/network/OtaRelease.cpp \
  'Custom OTA endpoint is fixed to the fork'
require_text 'isExpectedFirmwareUrl' src/network/OtaUpdater.cpp \
  'OTA validates selected repository and exact tag'
require_text 'releaseParser->finish()' src/network/OtaUpdater.cpp \
  'OTA rejects incomplete release metadata'
require_text 'esp_ota_begin(updatePartition, otaSize' src/network/OtaUpdater.cpp \
  'OTA begins the inactive slot with a known size'
require_text 'esp_ota_abort(otaHandle)' src/network/OtaUpdater.cpp \
  'OTA aborts failed or incomplete handles'
require_text 'downloadedSize != otaSize' src/network/OtaUpdater.cpp \
  'OTA checks received bytes against asset metadata'
require_text 'runningPartitionChipId' src/network/OtaUpdater.cpp \
  'OTA retains cross-chip image rejection'
require_text 'esp_wifi_set_ps(WIFI_PS_MIN_MODEM)' src/network/OtaUpdater.cpp \
  'OTA restores Wi-Fi power saving after transfer'
require_text 'extern "C" bool verifyRollbackLater() { return true; }' src/main.cpp \
  'early Arduino OTA confirmation is deferred'
require_text 'esp_ota_mark_app_valid_cancel_rollback' src/main.cpp \
  'healthy boot explicitly confirms the pending image'
require_text 'CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y' sdkconfig.defaults \
  'bootloader rollback remains enabled'
require_text 'CONFIG_APP_ROLLBACK_ENABLE=y' sdkconfig.defaults \
  'application rollback remains enabled'
require_text 'data, ota' partitions.csv 'partition table retains OTA selection data'
require_text 'app,  ota_0' partitions.csv 'partition table retains the first OTA slot'
require_text 'app,  ota_1' partitions.csv 'partition table retains the second OTA slot'

# TLS verification and settings-web heap hardening.
require_file scripts/freeink_sdk_patches/0001-verify-wolfssl-peer.patch
require_file scripts/patch_secure_net.py
require_file src/network/TrustedRootCertificates.cpp
require_text 'http.setCACert(TrustedRootCertificates::bundle())' src/network/HttpDownloader.cpp \
  'wolfSSL transport receives explicit trust anchors'
require_text 'wolfSSL_CTX_load_verify_buffer' scripts/freeink_sdk_patches/0001-verify-wolfssl-peer.patch \
  'SDK patch checks CA loading'
require_text 'wolfSSL_check_domain_name' scripts/freeink_sdk_patches/0001-verify-wolfssl-peer.patch \
  'SDK patch enables hostname verification'
require_text 'pre:scripts/patch_secure_net.py' platformio.ini \
  'verified-wolfSSL patch runs before compilation'
reject_text 'http.setInsecure();' src/network/HttpDownloader.cpp \
  'Drive/OTA transport does not disable certificate verification'
reject_text 'skip_cert_common_name_check = true' src/network/OtaUpdater.cpp \
  'OTA does not bypass hostname verification'
require_text 'inline const std::vector<SettingInfo>& getBaseSettingsList()' src/SettingsList.h \
  'settings base list is constructed once and borrowed by reference'
require_text 'const auto& settings = getBaseSettingsList();' src/network/CrossPointWebServer.cpp \
  'web settings handlers avoid a full settings-list copy'
require_text 'bool StreamingJsonParser::finish()' lib/JsonParser/StreamingJsonParser.cpp \
  'streaming JSON supports fail-closed finalization'

# Release workflow gates and idempotent stable publication.
require_text 'Validate tag matches firmware version' .github/workflows/release.yml \
  'release workflow validates exact tag/version equality'
require_text 'Audit fork invariants' .github/workflows/release.yml \
  'release workflow runs the fork audit'
require_text 'Run clang-format' .github/workflows/release.yml \
  'release workflow gates on clang-format 21'
require_text 'Run host unit tests' .github/workflows/release.yml \
  'release workflow gates on host tests'
require_text 'Run cppcheck' .github/workflows/release.yml \
  'release workflow gates on cppcheck'
require_text 'pio run -e gh_release' .github/workflows/release.yml \
  'release workflow builds the release environment'
require_text 'firmware.bin --clobber' .github/workflows/release.yml \
  'release asset upload is idempotent'
require_text '--draft=false --prerelease=false' .github/workflows/release.yml \
  'release reruns remain stable'

if ((failures > 0)); then
  printf '\nInvariant audit failed with %d issue(s). Do not build or publish a release.\n' "${failures}" >&2
  exit 1
fi

printf '\nAll textual fork invariants passed. Continue with semantic review, builds, CI, and X4 tests.\n'

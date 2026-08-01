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
  local label="$2"
  if git grep -Fq -- "${text}" -- '*.c' '*.cpp' '*.h' '*.hpp'; then
    fail "${label}"
  else
    pass "${label}"
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

branch="$(git branch --show-current)"
if [[ "${branch}" == release/* ]]; then
  branch_version="${branch#release/}"
  if [[ "${branch_version}" == "${config_version}" ]]; then
    pass "release branch matches firmware version"
  else
    fail "release branch ${branch} does not match firmware version ${config_version}"
  fi
else
  pass "working branch is ${branch:-detached}; release-branch equality deferred"
fi

if [[ ! "${config_version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  fail "fork version is an exact four-component numeric version"
else
  pass "fork version has four numeric components"
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

require_file src/activities/network/GoogleDriveSyncActivity.cpp
require_file src/activities/network/NetworkModeSelectionActivity.cpp
require_file src/GoogleDriveStore.cpp
require_file src/network/GoogleDriveClient.cpp
require_file src/network/GoogleDriveSyncPlan.cpp
require_file lib/JsonParser/DriveListJsonParser.h
require_file docs/google-drive-sync.md
require_file test/google_drive_sync_plan/GoogleDriveSyncPlanTest.cpp
require_text 'GOOGLE_DRIVE' src/activities/network/NetworkModeSelectionActivity.cpp \
  'Google Drive remains a File Transfer mode'
require_text 'gdriveLocalFolder' src/SettingsList.h 'Google Drive local mirror setting remains exposed'
require_text 'constexpr size_t MAX_TREE_ENTRIES = 300' src/network/GoogleDriveClient.h \
  'Drive tree remains explicitly bounded'
require_text 'remoteTree.nodes.reserve(GoogleDriveClient::MAX_TREE_ENTRIES)' \
  src/activities/network/GoogleDriveSyncActivity.cpp 'remote Drive tree is reserved before sync'
require_text 'localTree.reserve(GoogleDriveClient::MAX_TREE_ENTRIES)' \
  src/activities/network/GoogleDriveSyncActivity.cpp 'local Drive tree is reserved before sync'
require_text 'makeUniqueNoThrow<DriveListJsonParser>' src/network/GoogleDriveClient.cpp \
  'Drive response parser has fallible heap allocation'
require_text 'PART_SUFFIX[] = ".gdrive.part"' src/activities/network/GoogleDriveSyncActivity.cpp \
  'Drive downloads retain a temporary-file boundary'
require_text 'file.md5Checksum' src/activities/network/GoogleDriveSyncActivity.cpp \
  'Drive downloads retain checksum validation'
require_text 'bool finish() { return parser.finish(); }' lib/JsonParser/DriveListJsonParser.h \
  'Drive JSON exposes finalization'
require_text 'RenderLock lock(*this)' src/activities/network/GoogleDriveSyncActivity.cpp \
  'Drive render-visible state uses RenderLock'

require_file src/activities/settings/OtaUpdateActivity.cpp
require_file src/network/OtaRelease.cpp
require_file src/network/OtaVersion.cpp
require_file test/ota_release/OtaReleaseTest.cpp
require_file test/ota_version/OtaVersionTest.cpp
require_text 'crosspoint-reader/crosspoint-reader/releases/latest' src/network/OtaRelease.cpp \
  'Official OTA endpoint is fixed to upstream'
require_text 'trungbuivinh/crosspoint-reader/releases/latest' src/network/OtaRelease.cpp \
  'Custom OTA endpoint is fixed to the fork'
require_text 'isExpectedFirmwareUrl' src/network/OtaUpdater.cpp \
  'OTA validates asset repository and tag'
require_text 'releaseParser->finish()' src/network/OtaUpdater.cpp \
  'OTA rejects incomplete release metadata'
require_text 'esp_https_ota_abort' src/network/OtaUpdater.cpp \
  'OTA aborts failed or incomplete handles'
require_text 'downloadedSize != otaSize' src/network/OtaUpdater.cpp \
  'OTA checks downloaded bytes against asset metadata'
reject_text 'skip_cert_common_name_check = true' 'OTA TLS hostname verification is not bypassed'

require_text 'extern "C" bool verifyRollbackLater() { return true; }' src/main.cpp \
  'early Arduino OTA confirmation is deferred'
require_text 'esp_ota_mark_app_valid_cancel_rollback' src/main.cpp \
  'healthy boot explicitly confirms the OTA image'
require_text 'data, ota' partitions.csv 'partition table retains OTA selection data'
require_text 'app,  ota_0' partitions.csv 'partition table retains the first OTA application slot'
require_text 'app,  ota_1' partitions.csv 'partition table retains the second OTA application slot'
require_text 'inline const std::vector<SettingInfo>& getSettingsList()' src/SettingsList.h \
  'settings list is returned by const reference'
require_text 'bool StreamingJsonParser::finish()' lib/JsonParser/StreamingJsonParser.cpp \
  'streaming JSON supports fail-closed finalization'

require_text 'Validate tag matches firmware version' .github/workflows/release.yml \
  'release workflow validates tag/version equality'
require_text 'Run host unit tests' .github/workflows/release.yml \
  'release workflow gates on host tests'
require_text 'Run cppcheck' .github/workflows/release.yml \
  'release workflow gates on cppcheck'
require_text 'pio run -e gh_release' .github/workflows/release.yml \
  'release workflow builds the release environment'
require_text 'firmware.bin --clobber' .github/workflows/release.yml \
  'release asset upload is idempotent'
require_text '--draft=false --prerelease=false' .github/workflows/release.yml \
  'rerun release remains stable'

if ((failures > 0)); then
  printf '\nInvariant audit failed with %d issue(s). Do not build or publish a release.\n' "${failures}" >&2
  exit 1
fi

printf '\nAll textual fork invariants passed. Continue with semantic review, builds, and X4 tests.\n'

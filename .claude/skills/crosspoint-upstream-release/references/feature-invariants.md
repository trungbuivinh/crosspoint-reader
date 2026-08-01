# Personal-fork feature invariants

Use this as the semantic review contract while porting to a new upstream stable
release. Durable behavior matters more than retaining today's filenames. When
upstream moves a component, map the invariant to its replacement and update the
audit script only after verifying equivalence.

## Custom feature 1: Google Drive exact mirror

### User-visible behavior

- Google Drive remains a File Transfer mode with settings for a public folder
  ID, restricted API key, and dedicated local mirror root.
- The shared Drive folder is the source of truth. Directories, empty
  directories, EPUB contents, moves, casing, and deletions mirror recursively.
- Only EPUB files are downloaded. Unsupported Drive types are ignored.
- The SD root and protected system paths cannot be selected as the mirror root.
- Case-insensitive collisions, invalid FAT names, excessive depth, excessive
  path length, cycles, and trees above the bounded entry count fail before local
  mutation.
- The device shows planned creates/downloads/deletes and requires confirmation
  before destructive synchronization.

### Data-integrity ordering

- Download into a temporary artifact first.
- Validate the advertised byte count and Drive MD5 when available before
  replacing the final EPUB.
- Complete all required downloads before deleting local extras.
- Cancellation or any transfer/checksum/filesystem failure withholds the
  deletion phase and reports an incomplete sync.
- Startup recovery handles stale temporary/backup artifacts without treating a
  partial file as a successful mirror result.
- Test destructive behavior with a disposable Drive folder and backed-up SD
  content; never use the user's primary library for first validation.

### Constrained-device safety

- Route storage through `Storage`/`HalStorage` and `HalFile`; never bypass its SD
  mutex with direct SdFat access.
- Pre-reserve both bounded tree containers before Wi-Fi/TLS allocation.
- Allocate parser and reusable checksum/download scratch with no-throw helpers,
  null-check OOM, and avoid large loop-task stack buffers.
- Do not allocate in render or per-chunk hot paths. Avoid repeated temporary
  strings in case-insensitive tree matching.
- Protect fields shared between loop/task code and `render()` with
  `RenderLock`; rate-limit e-ink progress updates without rate-limiting Cancel
  polling.
- Parse each paginated Drive response to a complete, balanced JSON root. A
  truncated or mismatched response fails closed before plan approval.

### Regression tests

- Nested create/download, empty directories, same-size/different-MD5 updates,
  missing MD5, case correction, type conflicts, deep deletion order, empty
  remote behavior, and case-insensitive collisions.
- Truncated/mismatched Drive JSON and page-token pagination.
- Unknown/chunked content length still polls cancellation.

## Custom feature 2: dual-source OTA

### Source and version behavior

- Opening Check for updates always starts at a source selector; selection is
  not persisted.
- `Custom - by Trung Bui` is highlighted by default. Back exits before Wi-Fi is
  enabled. Official is an explicit choice.
- Official metadata uses only
  `crosspoint-reader/crosspoint-reader/releases/latest`; Custom uses only
  `trungbuivinh/crosspoint-reader/releases/latest`.
- The confirmation view identifies source, current version, and available
  version.
- Parse three- and four-component versions without heap allocation. Normalize
  three components to custom revision zero, compare all four numeric parts, and
  treat a stable release as newer than the matching current RC.
- Accept build/RC suffixes only for the running firmware. Reject malformed
  candidate tags; never use uninitialized parse output or downgrade.

### Metadata and transport trust

- Accept release metadata only after its full JSON root closes cleanly.
- Require a non-empty asset named exactly `firmware.bin` with positive size.
- Require its browser-download URL to match the selected repository and exact
  release tag. Reject cross-repository, query-appended, wrong-tag, empty, or
  partial values.
- Verify TLS certificate chain and hostname. Never restore
  `skip_cert_common_name_check = true` or an equivalent bypass.
- Keep the new upstream release's HTTP/TLS/OTA implementation. Port the selector
  and safety checks into it; do not replace it with the 1.4 downloader.

### Partition and rollback safety

- Write only the inactive OTA slot through the supported ESP-IDF/Arduino OTA
  API.
- Abort the OTA handle on HTTP, perform, completeness, or size failure. Do not
  finalize an incomplete handle.
- Require the received image length to match GitHub's asset size before making
  the slot bootable.
- Keep progress fields synchronized across render/update contexts and restore
  Wi-Fi power-saving state on all exits.
- Keep rollback pending through early boot. Mark the image valid only after
  storage/settings, display initialization, and usable activity routing (or a
  usable SD-error recovery screen) have completed.
- Retain an OTA-capable partition table with `otadata` and two non-overlapping
  application slots large enough for the candidate image. Confirm the built
  bootloader supports rollback before relying on the application checkpoint.
- If upstream changes Arduino's rollback hook or boot sequence, prove the new
  checkpoint from source and ELF behavior instead of retaining a stale symbol
  check.

### Regression tests

- `1.4.1.1 -> 1.4.1.2` and `1.4.1.2 -> 1.5.0.0` update.
- `1.5.0.1 -> 1.5.0`, equal, and older versions do not update.
- RC -> stable at the same numeric version updates.
- Malformed tags, missing/zero asset, truncated JSON, wrong repo/tag URL, and
  byte-count mismatch fail without changing the running slot.
- Official and Custom map to fixed, distinct endpoints.

## Cross-feature regressions from PR #4 and PR #6

- `getSettingsList()` remains a constructed-once `const` reference API (or an
  upstream equivalent with no per-request copy of the full settings vector).
  Google Drive accessors remain allocation-conscious function pointers rather
  than additions that trigger cppcheck value-flow failure or fragmented-heap
  aborts.
- Render-visible activity state is synchronized; no worker/render data race is
  introduced during conflict resolution.
- Streaming JSON consumers call a finalization/error check and reject
  incomplete documents.
- Large stack frames removed by PR #6 are not reintroduced. Review stack-usage
  output around Drive tree listing, local recursion/checksum, and OTA parsing.
- Full-tree formatting uses clang-format 21, matching CI. Run it after all new
  files exist, not only before adding port helpers.
- Release publication remains gated by exact tag/version match, formatting,
  host tests, cppcheck, and `gh_release`; reruns replace `firmware.bin`
  idempotently and do not create drafts/pre-releases.

## X4 production matrix

Record device model, previous version, candidate version, firmware hash, serial
log, and result for each item:

1. Prepare recovery first: retain a known-good full flash image and
   `firmware.bin`, verify USB flashing/serial access, charge the battery, and
   confirm partition offsets. Never interrupt bootloader, partition-table, or
   manual recovery flashing.
2. Boot candidate manually once; verify settings, reader navigation, SD error
   screen, and normal resume.
3. Google Drive: no-op sync, nested add/update/move, same-size content update,
   confirmed deletion, cancellation with known size, and cancellation with
   unknown/chunked size.
4. Google Drive failure: bad key/folder, truncated response if injectable,
   checksum mismatch, Wi-Fi loss, and full/removed SD. Confirm local extras are
   not deleted after transfer failure.
5. OTA selector: default Custom, Back before Wi-Fi, correct Official endpoint,
   correct Custom endpoint, same-version no-update, malformed metadata failure.
6. OTA success: Custom upgrades from the previous fork release, progress
   renders, reboot succeeds, version is correct, and Google Drive remains.
7. OTA interruption: network loss and power loss **only while application OTA
   is writing the inactive slot** both return to the previously valid image.
8. Rollback: a controlled test image that fails before the confirmation
   checkpoint rolls back; a healthy image is confirmed only after usable UI.
9. Official source: test only with explicit acceptance that it removes personal
   features, or validate the endpoint without installing it.

Any missing item remains a release blocker unless the user explicitly narrows
the release risk and documents that exception.

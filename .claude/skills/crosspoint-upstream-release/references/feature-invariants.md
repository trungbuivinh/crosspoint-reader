# Personal-fork feature invariants

Use this semantic contract when porting to a new stable upstream release.
Behavior and safety matter more than retaining current filenames. If upstream
moves a component, map the invariant to its replacement and update the audit
only after proving equivalence.

## Google Drive exact mirror

### User-visible behavior

- Google Drive remains a File Transfer mode with a public folder ID, restricted
  API key, and dedicated book folder on the device.
- Drive is the source of truth. Directories (including empty ones), EPUB
  contents, moves, letter case, and deletions mirror recursively.
- Only EPUB files are managed; Google-native and unsupported file types are
  ignored.
- SD root and protected system paths cannot be selected.
- Invalid FAT names, case-insensitive collisions, cycles, excessive depth/path
  length, and trees above the bounded entry count fail before mutation.
- The device previews create/download/rename/delete counts and confirms before
  destructive synchronization.

### Data-integrity ordering

- Download to a temporary artifact, validate advertised size and Drive MD5 when
  present, then atomically replace the destination through backup/rename.
- Finish all required downloads before deleting local extras.
- Cancellation, network loss, checksum mismatch, OOM, or filesystem failure
  withholds deletion and reports an incomplete sync.
- Startup recovery handles stale part/case/backup artifacts without treating a
  partial file as successful.
- First destructive validation uses a disposable Drive folder and backed-up SD.

### Constrained-device safety

- Route storage through `Storage`/`HalStorage` and `HalFile`; never bypass the SD
  mutex with direct SDK filesystem access.
- Start remote listing with a small capacity, grow in bounded steps only after
  HTTPS cleanup, allocate the local snapshot only after listing, and release
  retained capacity before retry.
- Heap-allocate the streaming parser and reusable checksum scratch with
  no-throw helpers and explicit OOM handling. Avoid large loop-task frames.
- Avoid allocation in render/per-chunk hot paths and repeated case-folded string
  copies in tree matching.
- Protect render-visible state with `RenderLock`. Rate-limit e-ink progress
  updates while continuing to poll Cancel for every received chunk, including
  unknown-length responses.
- Finalize every paginated JSON document; truncated or mismatched roots fail
  closed before plan approval.

### Regression tests

- Nested create/download, empty directories, same-size/different-MD5 updates,
  missing MD5, case correction, type conflicts, deep deletion order, empty
  remote, and case-insensitive collisions.
- Truncated/mismatched Drive JSON and page-token pagination.
- Known and unknown content lengths retain cancellation polling.

## Dual-source OTA

### Source and version behavior

- Check for updates always opens a non-persisted source selector.
- Custom is highlighted by default; Back exits before Wi-Fi; Official is an
  explicit choice.
- Official metadata is fixed to
  `crosspoint-reader/crosspoint-reader/releases/latest`; Custom is fixed to
  `trungbuivinh/crosspoint-reader/releases/latest`.
- Confirmation identifies source, current version, and available version.
- Parse three/four numeric components without heap allocation. Normalize an
  official three-component version to revision zero; compare all four parts;
  stable supersedes the matching running RC.
- Accept an optional leading `v` on official/candidate tags. Accept build/RC
  suffixes only for the running firmware. Reject malformed candidates and
  downgrades.

### Metadata and transport trust

- Accept metadata only after a complete, balanced JSON root.
- Require a non-empty exact `firmware.bin` asset with positive size.
- Require its browser-download URL to match the selected repository and exact
  tag; reject cross-repo, wrong-tag, query-appended, empty, or partial values.
- Verify TLS chains and hostnames end to end for the custom Drive/OTA transport.
  HTTPS redirects never downgrade to HTTP. Never use `setInsecure`,
  `skip_cert_common_name_check`, or an equivalent on those paths.
- On v1.5's wolfSSL transport, load only explicit roots, fail closed on CA-load
  failure or missing trust anchors, and configure `wolfSSL_check_domain_name`.
- Preserve upstream's current transport/OTA architecture; do not restore the
  1.4 downloader.

### Partition and rollback safety

- Write only the inactive slot through supported ESP-IDF APIs.
- Reject zero/oversized metadata before `esp_ota_begin`; pass the known size.
- Reject data beyond the advertised size, require exact bytes before
  `esp_ota_end`, and abort on HTTP/write/completeness/size failures.
- Preserve upstream's cross-chip image rejection before making a slot bootable.
- Keep progress synchronized and restore Wi-Fi power saving on all exits after
  it is disabled.
- Keep rollback pending through early boot. Confirm only after storage/settings,
  display, and usable routing complete, or after a usable SD-error recovery UI.
- Retain `otadata`, two non-overlapping application slots large enough for the
  image, and rollback-enabled bootloader/application config.
- If upstream changes the Arduino rollback hook, prove its replacement in source
  and ELF rather than retaining a stale symbol check.

### Regression tests

- Revision-to-revision and upstream-version updates; equal/older/downgrade do
  not update; RC-to-stable does.
- Malformed tags, missing/zero asset, truncated JSON, wrong repo/tag URL, and
  byte-count mismatch fail without changing the running slot.
- Official and Custom map to fixed, distinct endpoints.

## Cross-feature hardening

- Web GET/POST settings borrow a constructed-once base list instead of copying
  the full settings vector after Wi-Fi startup. The device UI may intentionally
  build a dynamic copy for SD fonts, sizes, dictionaries, and board filtering.
- Drive accessors remain allocation-conscious named functions where needed for
  cppcheck stability.
- Render/task state remains synchronized.
- All streaming JSON consumers call finalization and reject incomplete input.
- Large stack frames removed by prior hardening are not reintroduced; inspect
  Drive listing, local scan/checksum, and OTA parser stack reports.
- Full-tree formatting uses clang-format 21.
- Release publication enforces exact tag/version equality, audit, format, host
  tests, cppcheck, and `gh_release`; reruns replace `firmware.bin`
  idempotently and remain stable rather than draft/pre-release.

## X4 production matrix

Record device model, previous version, candidate version, firmware SHA-256,
serial log, and result for each item:

1. Prepare recovery: known-good full flash and `firmware.bin`, working USB/serial,
   charged battery, backed-up SD, and confirmed partition offsets. Never
   interrupt bootloader/partition-table/manual recovery flashing.
2. Manual candidate boot: settings, navigation, reading, SD-error UI, normal
   sleep/resume, touch/buttons, and version display.
3. Drive success: no-op; nested add/update/move; same-size content update;
   confirmed deletion; cancellation with known and unknown/chunked size.
4. Drive failure: bad key/folder, injectable truncation/checksum mismatch, Wi-Fi
   loss, full/removed SD. Local extras must survive failed downloads.
5. OTA selector: Custom default, Back before Wi-Fi, correct Official/Custom
   endpoints, same-version no-update, malformed metadata failure.
6. Custom OTA success from the previous fork release: progress, reboot, exact
   version, settings/reader health, and Google Drive retained.
7. OTA interruption: network loss and power loss only while application OTA is
   writing the inactive slot; both recover the previously valid image.
8. Rollback: a controlled image that fails before confirmation rolls back; a
   healthy image is confirmed only after usable UI.
9. Official source: install only with explicit acceptance that it removes fork
   features; otherwise verify endpoint/metadata without installation.

Any missing item blocks production publication unless the user explicitly
narrows the release risk and records that exception.

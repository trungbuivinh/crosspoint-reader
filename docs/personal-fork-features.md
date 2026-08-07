# Personal Fork Features

This repository is a personal fork of CrossPoint Reader. Custom behavior is
kept in reviewable commits so it can be audited, tested, and ported whenever a
new stable upstream release becomes the base.

## Feature inventory

1. **Google Drive EPUB exact mirror**

   Recursively mirrors a link-shared Drive folder to a dedicated book folder on
   the SD card. Drive remains the source of truth for additions, contents,
   moves, letter case, and deletions. See
   [Google Drive Sync](./google-drive-sync.md).

2. **Dual-source OTA updates**

   Adds a source selector before the upstream OTA flow, while retaining strict
   metadata, transport, inactive-slot, and rollback checks.

## Dual-source OTA behavior

The source selector appears every time **Settings → Check for updates** opens.
It is not persisted.

- **Custom - by Trung Bui** is selected by default and checks the latest stable
  release from `trungbuivinh/crosspoint-reader`.
- **Official version** checks the latest stable release from
  `crosspoint-reader/crosspoint-reader`.
- Wi-Fi starts only after confirmation. Back exits without enabling Wi-Fi.
- Confirmation shows the selected source, running version, and available
  version.

Both sources use v1.5's wolfSSL downloader and ESP-IDF OTA partition APIs.
Release metadata must be a complete JSON document with a valid three- or
four-component tag and one non-empty asset named exactly `firmware.bin`. Its URL
must match the selected repository and exact tag. HTTPS verifies both chain and
hostname, and an HTTPS redirect cannot downgrade to HTTP.

The candidate must fit the inactive slot. The advertised and received byte
counts must match before the image is finalized, and the existing cross-chip
guard rejects firmware for another device family. Transfer failure aborts the
OTA handle and leaves the running slot unchanged.

Rollback remains armed through early startup. A new image is marked valid only
after storage/settings, display initialization, and usable activity routing—or
after the usable SD-error screen is installed when removable media is absent.
A crash before that checkpoint leaves the image pending so the bootloader can
return to the previous slot.

Selecting Official is an explicit opt-out from this personal fork. Installing
official firmware can remove both custom features, including Google Drive sync.

## Version and release policy

The current target is **1.5.0.1**, based on upstream stable **v1.5.0**. The
release branch, `[crosspoint] version`, embedded production version, exact tag,
and GitHub release title must agree.

Every upstream upgrade uses a fresh release branch from the verified stable tag
commit. Historical release branches are not rebased or force-pushed. The
project-local `crosspoint-upstream-release` workflow under
`.claude/skills/crosspoint-upstream-release/` inventories the old delta and
ports both feature contracts into the current upstream architecture.

The tag workflow validates tag/version equality, runs the invariant audit,
clang-format 21, all host tests, cppcheck, and the `gh_release` build. It creates
or updates a stable release and uploads one `firmware.bin` idempotently.

Compilation and green CI are necessary but not sufficient. Production tagging
also requires the X4 matrix in the invariant reference, using a backed-up SD
card and disposable Drive mirror. Missing interrupted-OTA, rollback, or
destructive-sync evidence blocks publication unless an explicit risk exception
is recorded.

## Stabilization checklist

- Source selector opens with Custom highlighted; Back does not start Wi-Fi.
- Official and Custom query only their fixed repositories.
- Equal, older, malformed, cross-repository, truncated, zero-size, oversized,
  or byte-mismatched candidates cannot change the boot slot.
- Custom OTA from the previous production fork boots 1.5.0.1 and retains Drive.
- A controlled early-boot failure rolls back; a healthy image confirms only at
  the late checkpoint.
- Network/power interruption while writing the inactive slot retains the
  previous valid image.
- Drive no-op, add/update/move/case/delete, cancellation, checksum failure,
  Wi-Fi loss, and full/removed SD follow the exact-mirror safety contract.

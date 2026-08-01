# Personal Fork Features

This repository is a personal fork of CrossPoint Reader. Its custom features are
kept as separate changes so they can be audited, tested, and carried forward when
a new upstream release becomes the base.

## Feature inventory

1. **Google Drive EPUB mirror**

   Recursively mirrors a link-shared Google Drive folder to a dedicated SD-card
   directory. The Drive tree remains the source of truth for additions, content
   changes, moves, and deletions. See [Google Drive Sync](./google-drive-sync.md).

2. **Dual-source OTA updates**

   Adds an update-source selector in front of the existing OTA flow. This is the
   second personal feature of the fork.

## Dual-source OTA behavior

The source selector appears every time **Settings -> Check for updates** is
opened. The selection is not saved in Settings.

- **Custom - by Trung Bui** is selected by default and checks the latest stable
  release from `trungbuivinh/crosspoint-reader`.
- **Official** checks the latest stable release from
  `crosspoint-reader/crosspoint-reader`.
- Wi-Fi is enabled only after the source is confirmed. Back exits to Settings
  without enabling Wi-Fi.
- Before installation, the confirmation screen shows the selected source, the
  running version, and the available version.

Both sources use the existing OTA download, progress, flash, and reboot path.
Release metadata must contain a valid three- or four-component version tag and an
asset named exactly `firmware.bin`. A missing release or asset, malformed tag, or
HTTP failure stops with **Update failed** and does not replace the running
firmware.

Selecting **Official** is an explicit opt-out from the personal fork. An official
firmware can remove both personal features, including Google Drive sync.

## Version and release policy

The current stabilization baseline is **1.4.1.0** on branch
`release/1.4.1.0`. The feature does not introduce a different version number on
this branch: firmware metadata, branch name, and any test tag must remain aligned
at `1.4.1.0`.

After the dual-source OTA feature is verified stable on hardware, it will be
ported as a distinct change to the next release branch. The next branch will be
created from that release's upstream base; this historical release branch will
not be rebased or force-pushed. When porting, preserve the OTA download stack of
the new upstream release rather than replacing it with the 1.4 implementation.

For custom OTA publication, the tag must exactly match the `[crosspoint] version`
in `platformio.ini`. The release workflow builds `gh_release`, publishes a stable
GitHub Release, and attaches the generated image as `firmware.bin`. Drafts and
pre-releases are intentionally excluded from the `/releases/latest` channel.

## Stabilization checklist

- The selector always opens with Custom highlighted.
- Back from the selector does not enable Wi-Fi.
- Official and Custom query their respective repositories.
- Equal or older releases show **No update available** and cannot be installed.
- Malformed metadata and network failures leave the installed firmware intact.
- A newer custom release completes download, flash, reboot, and reports the new
  version.
- Google Drive sync remains available after a custom OTA update.

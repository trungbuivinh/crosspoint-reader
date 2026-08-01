# Google Drive Sync

Google Drive Sync turns a link-shared Drive folder into the source of truth for
a dedicated EPUB library folder on the reader. The reader pulls the complete
folder hierarchy over Wi-Fi, so Drive directories can be used to classify
books.

It is the fourth mode on the **File Transfer** screen, alongside Join a Network,
Calibre Wireless, and Create Hotspot.

## Mirror behavior

The selected local folder represents the configured shared Drive folder. The
Drive folder's children are placed directly inside it; no extra wrapper folder
is created.

```text
Drive: Shared Library/          SD card: /Books/Drive/
├── Fiction/                    ├── Fiction/
│   └── Dune.epub               │   └── Dune.epub
├── Technical/                  ├── Technical/
│   └── C++.epub                │   └── C++.epub
└── To Read/                    └── To Read/
```

- Every Drive directory is mirrored recursively, including empty directories.
- Only `.epub` files are downloaded. Other Drive files, Google Docs, Sheets,
  Slides, and shortcuts are ignored.
- The local target is a dedicated managed mirror. Any local file or directory
  absent from the managed Drive tree is deleted after an on-device warning and
  confirmation. This includes local non-EPUB files.
- Local EPUB content is compared with Drive's MD5 checksum, not just file size.
  Local edits and same-size Drive updates are therefore replaced by the Drive
  version.
- A successful sync means the relative directory paths, EPUB paths, and EPUB
  contents match Drive exactly.
- Path casing is corrected to match Drive even though the SD-card filesystem
  itself compares names case-insensitively.

> [!WARNING]
> Do not select a folder containing files that are not managed through the
> shared Drive library. Those files will be deleted during sync. The SD-card
> root `/` cannot be selected.

## One-time setup

### 1. Create and share a Drive folder

1. Create the Drive folder and any category subfolders you want.
2. Right-click the root folder and choose **Share**.
3. Set **General access** to **Anyone with the link**, role **Viewer**.
4. Copy the folder ID from the final segment of its URL:

   ```text
   https://drive.google.com/drive/folders/1AbCdEfGhIjKlMnOpQrStUvWxYz012345
                                            └──────────── folder ID ───────────┘
   ```

### 2. Create a Google API key

1. In the [Google Cloud Console](https://console.cloud.google.com/), create or
   select a project.
2. Enable **Google Drive API**.
3. Open **APIs & Services → Credentials → Create Credentials → API key**.
4. Restrict the key to Google Drive API when possible.

There is no Google account sign-in on the reader. The API key can only access
content made public through link sharing.

### 3. Select the local mirror

1. On the reader, open **File Transfer → Join a Network**.
2. Open `http://<device-ip>/settings` or
   `http://crosspoint.local/settings` from a browser.
3. In **Google Drive Sync**, set:
   - **Drive Folder ID**
   - **Drive API Key**
   - **Local Mirror Folder** — use **Choose** to browse the SD card
4. Save settings.

The local target must already exist and cannot be `/` or a protected system
directory. Existing installations must select a local target before their next
sync; the previous SD-root behavior is not retained because exact mirroring can
delete local extras.

## Running a sync

1. Open **File Transfer → Google Drive**.
2. The reader connects to Wi-Fi, recursively lists Drive, scans the local tree,
   and hashes matching EPUBs.
3. Review the counts for folders to create, EPUBs to update, and local files or
   directories to delete.
4. Press **Start Sync**. If deletion is planned, confirm the warning.
5. Downloads and checksum verification run first. Local extras are deleted only
   after every download succeeds.

Press **Back** during the download phase to cancel. Incomplete downloads are
discarded and the deletion phase is withheld; downloads that already completed
may remain. The result is reported as incomplete rather than successful.

Drive listing responses must be complete, structurally balanced JSON documents;
a truncated response is rejected before a mirror plan can be approved. Downloads
are written to a temporary file and must match both the advertised byte size and,
when supplied by Drive, the MD5 checksum before replacing the existing EPUB.
Progress rendering is rate-limited so e-ink refresh work does not starve the TLS
transfer, while the Cancel button continues to be polled for chunked responses.

## Limits and unsupported trees

- Maximum 300 nodes per side: Drive directories/EPUBs remotely and all entries
  inside the selected local target.
- Maximum directory depth: 16.
- Maximum path supported by the mirror: 240 UTF-8 bytes including the selected
  local root.
- A path component may use up to 100 UTF-8 bytes.
- Duplicate names that collide on a case-insensitive FAT filesystem are
  rejected before local changes are made.
- Drive names that cannot be represented losslessly on FAT are rejected rather
  than silently renamed.
- Parser and checksum scratch storage is bounded and allocated with explicit
  out-of-memory handling; the activity preallocates its two 300-entry tree
  arrays before Wi-Fi/TLS starts to reduce fragmentation.

## Troubleshooting

| Symptom | Likely cause |
|---------|--------------|
| Google Drive is not configured | Folder ID, API key, or Local Mirror Folder is missing. |
| Failed to fetch the Drive tree | Folder is not link-shared, the ID/key is wrong, Drive API is disabled, or the network is offline. |
| Name collision or invalid name | Two siblings differ only by case, or a Drive name is not FAT-compatible. |
| Sync incomplete | A download, checksum, filesystem operation, or user cancellation prevented an exact result. Deletions were withheld if transfer failed. |
| Local file disappeared | The selected target is managed by Drive; local extras are intentionally deleted after confirmation. |

The personal diagnostic build adds the HTTP stage, HTTP status, and transport
error to the Drive-tree error screen. Its serial log also records the first 191
bytes of a non-200 server response, which lets the owner distinguish a bad API
key, sharing restriction, quota problem, or TLS/network failure without
approving a mirror plan.

## Related documentation

- [User Guide](../USER_GUIDE.md)
- [Web Server Guide](./webserver.md)
- [Webserver Endpoints](./webserver-endpoints.md)

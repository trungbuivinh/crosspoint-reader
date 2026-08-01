# Google Drive Sync

Google Drive Sync is a **pull**-based transfer method: instead of pushing files
to the device from a computer, the reader fetches books from a shared Google
Drive folder over Wi-Fi. Drop books into the folder from any device, then run a
one-button sync on the reader to download everything new.

It is the fourth mode on the **File Transfer** screen, alongside Join a Network,
Calibre Wireless, and Create Hotspot.

## How it works

- You share one Google Drive folder as **"anyone with the link"** and create a
  free Google API key.
- You enter the **folder ID** and **API key** once, from the web settings page.
- On the device, **File Transfer → Google Drive** connects to Wi-Fi, lists the
  folder, and downloads every supported book that is missing locally or whose
  size differs from the copy on Drive.

There is no Google sign-in. The reader uses the public Google Drive API v3 with
your API key, so only a link-shared folder is reachable. This is a deliberate
constraint: Google's device-login flow cannot grant access to your private
Drive files, and the reader keeps no account credentials.

> [!IMPORTANT]
> A folder shared as "anyone with the link" is readable by anyone who has the
> link. Use a folder created specifically for your books, not one holding
> private material.

## One-time setup

### 1. Create and share a Drive folder

1. In Google Drive, create a folder for your books (for example, `CrossPoint`).
2. Right-click the folder → **Share**.
3. Under **General access**, change **Restricted** to **Anyone with the link**,
   with the role **Viewer**.
4. Copy the folder link. The **folder ID** is the last path segment:

   ```text
   https://drive.google.com/drive/folders/1AbCdEfGhIjKlMnOpQrStUvWxYz012345
                                            └──────────── folder ID ───────────┘
   ```

### 2. Create a Google API key

1. Open the [Google Cloud Console](https://console.cloud.google.com/).
2. Create a project (or select an existing one).
3. Go to **APIs & Services → Library**, search for **Google Drive API**, and
   click **Enable**.
4. Go to **APIs & Services → Credentials → Create Credentials → API key**.
5. Copy the generated key.
6. (Recommended) Click the key to restrict it: under **API restrictions**,
   choose **Restrict key** and select only **Google Drive API**. This limits
   what the key can do if it leaks.

The free tier's default quota is far more than a personal library needs.

### 3. Enter the configuration on the device

The folder ID and API key are long strings, so enter them from a browser rather
than typing on the device:

1. On the reader, open **File Transfer → Join a Network** and connect to Wi-Fi.
2. In a browser on the same network, open `http://<device-ip>/settings` (the IP
   is shown on the reader), or `http://crosspoint.local/settings`.
3. Find the **Google Drive Sync** section and fill in:
   - **Drive Folder ID** — the folder ID from step 1.
   - **Drive API Key** — the key from step 2.
4. Save. The API key is stored obfuscated on the SD card
   (`/.crosspoint/gdrive.json`) and is never shown back in the web UI.

You can also create `/.crosspoint/gdrive.json` by hand, but the web page is the
supported path and validates the values as you go.

## Syncing books

1. On the reader, open **File Transfer → Google Drive**.
2. The reader connects to Wi-Fi (reusing your saved networks), then lists the
   folder and shows how many files it found and how many need downloading.
3. Press **Start Sync**. Each file downloads in turn with a per-file and an
   overall progress bar.
4. Press **Back** at any time to cancel; the in-progress file is discarded
   cleanly.
5. When finished, the reader shows a summary: how many books were downloaded and
   how many failed.

Books download to the SD-card root, so they appear immediately in **Browse
Files** and **Recent Books**.

### What gets synced

- Supported book formats only: `.epub`, `.txt`, `.xtc`, `.xtch`, and `.md`.
  Other file types in the folder are ignored.
- Google-native files (Docs, Sheets, Slides) are skipped — they have no
  downloadable book content.
- A file is downloaded when it is **missing** on the SD card or when the local
  size **differs** from the copy on Drive. Files already present at the same
  size are left alone, so re-running a sync only fetches what changed.
- Up to 300 files are listed per sync. Larger folders are paginated; if you keep
  more than 300 books in one folder, split them across subfolders (only the
  configured folder's direct contents are synced — subfolders are not scanned).

When a book is overwritten by a newer copy, its cached metadata is cleared so
the reader re-parses it on next open.

## Troubleshooting

| Symptom | Likely cause |
|---------|--------------|
| "Google Drive is not configured" | Folder ID or API key is empty — set both in web settings. |
| "Failed to list Drive folder" | Folder is not shared as "anyone with the link"; wrong folder ID; API key invalid or Drive API not enabled; no internet on the joined network. |
| A book never downloads | Unsupported extension, or it is a Google-native Doc/Sheet/Slide. |
| Some files fail but the sync finishes | Individual download errors are skipped so one bad file does not stop the rest; the summary reports the failure count. |

## Related documentation

- [User Guide](../USER_GUIDE.md)
- [Web Server Guide](./webserver.md)
- [Webserver Endpoints](./webserver-endpoints.md)

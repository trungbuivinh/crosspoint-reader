#pragma once

#include <cstdint>
#include <string>
#include <vector>

// One downloadable, supported file from the configured Drive folder.
struct DriveFileEntry {
  std::string id;
  std::string name;
  uint32_t size;
};

/**
 * Minimal Google Drive API v3 client for a link-shared ("anyone with the
 * link") folder accessed with an API key. No OAuth: both listing and
 * download are plain verified-HTTPS GETs through HttpDownloader.
 */
namespace GoogleDriveClient {

// Hard cap on collected entries. RAM bound: ~150 B/entry (two heap strings
// + 4 B) -> ~45 KB worst case, kept because entries persist through the
// whole sync while TLS sessions come and go.
constexpr size_t MAX_FILES = 300;

/**
 * Lists the folder (paginated, pageSize=100), keeping only supported book
 * extensions and skipping Google-native docs (no binary content). Results
 * are appended to `out` (cleared first). Returns false on HTTP/parse error.
 */
bool listFolder(const std::string& folderId, const std::string& apiKey, std::vector<DriveFileEntry>& out);

/** Direct-download URL for a file (alt=media). */
std::string downloadUrl(const std::string& fileId, const std::string& apiKey);

}  // namespace GoogleDriveClient

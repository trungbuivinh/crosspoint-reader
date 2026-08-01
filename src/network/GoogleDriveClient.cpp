#include "GoogleDriveClient.h"

#include <DriveListJsonParser.h>
#include <FsHelpers.h>
#include <Logging.h>

#include <cstring>

#include "HttpDownloader.h"
#include "util/UrlUtils.h"

namespace {

constexpr char DRIVE_FILES_URL[] = "https://www.googleapis.com/drive/v3/files";

bool isSupportedBookFile(const char* name) {
  const std::string_view view{name};
  return FsHelpers::hasEpubExtension(view) || FsHelpers::hasXtcExtension(view) || FsHelpers::hasTxtExtension(view) ||
         FsHelpers::hasMarkdownExtension(view);
}

void collectFile(void* ctx, const DriveFileInfo& file) {
  auto* out = static_cast<std::vector<DriveFileEntry>*>(ctx);
  if (out->size() >= GoogleDriveClient::MAX_FILES) {
    return;
  }
  // Google-native docs (Docs/Sheets/...) have no binary content to download
  if (strncmp(file.mimeType, "application/vnd.google-apps", 27) == 0) {
    return;
  }
  if (!isSupportedBookFile(file.name)) {
    return;
  }
  out->push_back({file.id, file.name, file.size});
}

}  // namespace

namespace GoogleDriveClient {

bool listFolder(const std::string& folderId, const std::string& apiKey, std::vector<DriveFileEntry>& out) {
  out.clear();
  out.reserve(64);  // typical folder fits one page; grows toward MAX_FILES only for large libraries

  DriveListJsonParser parser(&out, collectFile);
  std::string pageToken;
  int page = 0;

  do {
    // The q parameter value is pre-encoded: '+' for spaces, %3D for '='
    std::string url = DRIVE_FILES_URL;
    url += "?q='" + folderId + "'+in+parents+and+trashed%3Dfalse";
    url += "&key=" + apiKey;
    url += "&fields=nextPageToken,files(id,name,size,mimeType)";
    url += "&pageSize=100";
    if (!pageToken.empty()) {
      url += "&pageToken=" + UrlUtils::urlEncode(pageToken);
    }

    parser.reset();
    const bool ok = HttpDownloader::fetchUrl(url, [&parser](const uint8_t* data, size_t len) {
      parser.feed(reinterpret_cast<const char*>(data), len);
      return true;
    });

    if (!ok || parser.hasError()) {
      LOG_ERR("GDRV", "Listing failed on page %d (http=%d, parse=%d)", page, ok ? 1 : 0, parser.hasError() ? 1 : 0);
      return false;
    }

    pageToken = parser.getNextPageToken();
    page++;
  } while (!pageToken.empty() && out.size() < MAX_FILES);

  LOG_DBG("GDRV", "Listed %zu supported files in %d page(s)", out.size(), page);
  return true;
}

std::string downloadUrl(const std::string& fileId, const std::string& apiKey) {
  return std::string(DRIVE_FILES_URL) + "/" + fileId + "?alt=media&key=" + apiKey;
}

}  // namespace GoogleDriveClient

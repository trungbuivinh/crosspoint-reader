#include "GoogleDriveClient.h"

#include <DriveListJsonParser.h>
#include <FsHelpers.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <deque>
#include <unordered_set>
#include <utility>
#include <vector>

#include "HttpDownloader.h"
#include "util/UrlUtils.h"

namespace {

constexpr char DRIVE_FILES_URL[] = "https://www.googleapis.com/drive/v3/files";
constexpr char DRIVE_FOLDER_MIME[] = "application/vnd.google-apps.folder";
constexpr char GOOGLE_APPS_PREFIX[] = "application/vnd.google-apps.";

struct ParsedChild {
  std::string id;
  std::string name;
  std::string mimeType;
  std::string md5Checksum;
  uint32_t size = 0;
  bool truncated = false;
};

struct PageCollector {
  std::vector<ParsedChild> children;
};

struct PendingFolder {
  std::string id;
  std::string relativePath;
  size_t depth = 0;
};

void collectChild(void* ctx, const DriveFileInfo& file) {
  auto* collector = static_cast<PageCollector*>(ctx);
  collector->children.push_back({file.id, file.name, file.mimeType, file.md5Checksum, file.size,
                                 file.idTruncated || file.nameTruncated || file.md5Truncated});
}

std::string caseFoldAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool isValidComponent(const std::string& name) {
  if (name.empty() || name.size() > GoogleDriveClient::MAX_COMPONENT_BYTES || name == "." || name == "..") {
    return false;
  }
  if (name.back() == ' ' || name.back() == '.') {
    return false;
  }
  const bool containsInvalidCharacter = std::any_of(name.begin(), name.end(), [](const unsigned char c) {
    return c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
           c == '|';
  });
  if (containsInvalidCharacter) return false;
  const std::string folded = caseFoldAscii(name);
  return !folded.ends_with(".gdrive.part") && !folded.ends_with(".gdrive.bak") && !folded.ends_with(".gdrive.case");
}

std::string joinRelative(const std::string& parent, const std::string& child) {
  return parent.empty() ? child : parent + "/" + child;
}

bool isFolder(const ParsedChild& child) { return child.mimeType == DRIVE_FOLDER_MIME; }

bool isEpub(const ParsedChild& child) { return FsHelpers::hasEpubExtension(child.name); }

bool isGoogleNative(const ParsedChild& child) { return child.mimeType.rfind(GOOGLE_APPS_PREFIX, 0) == 0; }

}  // namespace

namespace GoogleDriveClient {

DriveTreeResult listTree(const std::string& folderId, const std::string& apiKey, DriveTree& out) {
  out.nodes.clear();
  out.nodes.reserve(64);

  std::deque<PendingFolder> pending;
  pending.push_back({folderId, "", 0});
  std::unordered_set<std::string> visited;
  visited.insert(folderId);

  // Keep the large streaming parser off the 8 KiB Arduino loop stack and
  // reuse both allocations for every response page. Page size is capped at
  // 100 by the request below.
  PageCollector collector;
  collector.children.reserve(100);
  auto parser = makeUniqueNoThrow<DriveListJsonParser>(&collector, collectChild);
  if (!parser) {
    LOG_ERR("GDRV", "OOM allocating Drive response parser");
    return DriveTreeResult::OOM;
  }

  while (!pending.empty()) {
    PendingFolder folder = std::move(pending.front());
    pending.pop_front();

    std::unordered_set<std::string> siblingNames;
    std::string pageToken;
    int page = 0;

    do {
      std::string url = DRIVE_FILES_URL;
      url += "?q='" + folder.id + "'+in+parents+and+trashed%3Dfalse";
      url += "&key=" + apiKey;
      url += "&fields=nextPageToken,files(id,name,size,mimeType,md5Checksum)";
      url += "&pageSize=100";
      if (!pageToken.empty()) {
        url += "&pageToken=" + UrlUtils::urlEncode(pageToken);
      }

      collector.children.clear();
      parser->reset();
      const bool ok = HttpDownloader::fetchUrl(url, [&parser](const uint8_t* data, size_t len) {
        parser->feed(reinterpret_cast<const char*>(data), len);
        return true;
      });
      if (!ok) {
        LOG_ERR("GDRV", "Tree listing HTTP failure at %s page %d", folder.relativePath.c_str(), page);
        return DriveTreeResult::HTTP_ERROR;
      }
      if (!parser->finish()) {
        LOG_ERR("GDRV", "Tree listing parse failure at %s page %d", folder.relativePath.c_str(), page);
        return DriveTreeResult::PARSE_ERROR;
      }

      for (const auto& child : collector.children) {
        const bool managed = isFolder(child) || (!isGoogleNative(child) && isEpub(child));
        if (!managed) {
          continue;
        }
        if (child.truncated || !isValidComponent(child.name)) {
          return DriveTreeResult::INVALID_NAME;
        }

        const std::string foldedName = caseFoldAscii(child.name);
        if (!siblingNames.insert(foldedName).second) {
          return DriveTreeResult::NAME_COLLISION;
        }

        const std::string relativePath = joinRelative(folder.relativePath, child.name);
        if (relativePath.size() > MAX_LOCAL_PATH_BYTES) {
          return DriveTreeResult::PATH_TOO_LONG;
        }
        if (out.nodes.size() >= MAX_TREE_ENTRIES) {
          return DriveTreeResult::TOO_MANY_ENTRIES;
        }

        if (isFolder(child)) {
          const size_t childDepth = folder.depth + 1;
          if (childDepth > MAX_TREE_DEPTH) {
            return DriveTreeResult::TOO_DEEP;
          }
          if (!visited.insert(child.id).second) {
            return DriveTreeResult::CYCLE_DETECTED;
          }
          out.nodes.push_back({DriveNodeType::DIRECTORY, child.id, child.name, relativePath, 0, ""});
          pending.push_back({child.id, relativePath, childDepth});
        } else {
          out.nodes.push_back({DriveNodeType::EPUB, child.id, child.name, relativePath, child.size, child.md5Checksum});
        }
      }

      pageToken = parser->getNextPageToken();
      page++;
    } while (!pageToken.empty());
  }

  LOG_DBG("GDRV", "Listed recursive tree with %zu managed node(s)", out.nodes.size());
  return DriveTreeResult::OK;
}

std::string downloadUrl(const std::string& fileId, const std::string& apiKey) {
  return std::string(DRIVE_FILES_URL) + "/" + fileId + "?alt=media&key=" + apiKey;
}

}  // namespace GoogleDriveClient

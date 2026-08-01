#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class DriveNodeType : uint8_t { DIRECTORY, EPUB };

struct DriveNode {
  DriveNodeType type;
  std::string id;
  std::string name;
  std::string relativePath;
  uint32_t size = 0;
  std::string md5Checksum;
};

struct DriveTree {
  std::vector<DriveNode> nodes;
};

/**
 * Minimal Google Drive API v3 client for a link-shared ("anyone with the
 * link") folder accessed with an API key. No OAuth: both listing and
 * download are plain verified-HTTPS GETs through HttpDownloader.
 */
namespace GoogleDriveClient {

constexpr size_t MAX_TREE_ENTRIES = 300;
constexpr size_t MAX_TREE_DEPTH = 16;
constexpr size_t MAX_COMPONENT_BYTES = 100;
constexpr size_t MAX_LOCAL_PATH_BYTES = 240;

enum class DriveTreeResult {
  OK,
  OOM,
  HTTP_ERROR,
  PARSE_ERROR,
  TOO_MANY_ENTRIES,
  TOO_DEEP,
  PATH_TOO_LONG,
  INVALID_NAME,
  NAME_COLLISION,
  CYCLE_DETECTED,
};

/**
 * Recursively lists a Drive folder. Every directory and direct EPUB child is
 * retained; other file types are ignored. The configured Drive folder maps to
 * the local sync root, so returned paths are relative to that root.
 */
DriveTreeResult listTree(const std::string& folderId, const std::string& apiKey, DriveTree& out);

/** Direct-download URL for a file (alt=media). */
std::string downloadUrl(const std::string& fileId, const std::string& apiKey);

}  // namespace GoogleDriveClient

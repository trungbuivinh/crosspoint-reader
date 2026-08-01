#pragma once

#include <cstddef>
#include <cstdint>

#include "StreamingJsonParser.h"

// One completed entry from a Google Drive files.list response page.
// Fixed buffers: emitted by value through the callback, never heap-allocated.
struct DriveFileInfo {
  char id[64];        // Drive file IDs are 28-44 chars
  char name[192];     // longer names are truncated
  char mimeType[64];  // enough to distinguish application/vnd.google-apps.* prefixes
  char md5Checksum[33];
  uint32_t size;  // 0 when absent (Google-native docs have no size)
  bool idTruncated;
  bool nameTruncated;
  bool md5Truncated;
};

/**
 * Streaming SAX parser for Google Drive API v3 files.list responses:
 *   {"nextPageToken":"...","files":[{"id":"...","name":"...","mimeType":"...","size":"123"},...]}
 *
 * Feeds chunks as they arrive from the HTTP client (no body buffering).
 * Each completed file object is emitted through the callback; the caller
 * decides what to keep. Note the Drive API sends "size" as a JSON string.
 */
class DriveListJsonParser {
 public:
  using FileCallback = void (*)(void* ctx, const DriveFileInfo& file);

  DriveListJsonParser(void* callbackCtx, FileCallback callback);

  DriveListJsonParser(const DriveListJsonParser&) = delete;
  DriveListJsonParser& operator=(const DriveListJsonParser&) = delete;

  // Reset all state for the next response page.
  void reset();
  void feed(const char* data, size_t len);

  bool hasError() const { return parser.hasError(); }
  // Empty string when the response had no nextPageToken (last page).
  const char* getNextPageToken() const { return nextPageToken; }

 private:
  enum class Position : uint8_t {
    TOP_LEVEL,
    IN_FILES_ARRAY,
    IN_FILE_OBJECT,
  };

  enum class LastKey : uint8_t {
    NONE,
    NEXT_PAGE_TOKEN,
    FILES,
    FILE_ID,
    FILE_NAME,
    FILE_MIME,
    FILE_SIZE,
    FILE_MD5,
  };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnNull(void* ctx);
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  void commitFile();

  StreamingJsonParser parser;

  void* fileCtx;
  FileCallback onFile;

  Position position;
  LastKey lastKey;
  uint8_t depth;
  uint8_t fileDepth;

  char nextPageToken[224];  // observed tokens are <200 chars
  DriveFileInfo current;
};

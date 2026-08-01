#include "DriveListJsonParser.h"

#include <cstdlib>
#include <cstring>

namespace {

void safeCopy(char* dst, size_t dstSize, const char* src, size_t srcLen) {
  size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

}  // namespace

DriveListJsonParser::DriveListJsonParser(void* callbackCtx, FileCallback callback)
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}),
      fileCtx(callbackCtx),
      onFile(callback) {
  reset();
}

void DriveListJsonParser::reset() {
  parser.reset();
  position = Position::TOP_LEVEL;
  lastKey = LastKey::NONE;
  depth = 0;
  fileDepth = 0;
  nextPageToken[0] = '\0';
  current.id[0] = '\0';
  current.name[0] = '\0';
  current.mimeType[0] = '\0';
  current.size = 0;
}

void DriveListJsonParser::feed(const char* data, size_t len) { parser.feed(data, len); }

void DriveListJsonParser::commitFile() {
  if (onFile && current.id[0] != '\0') {
    onFile(fileCtx, current);
  }
  current.id[0] = '\0';
  current.name[0] = '\0';
  current.mimeType[0] = '\0';
  current.size = 0;
}

// -- SAX callbacks (static trampolines) --------------------------------------

void DriveListJsonParser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<DriveListJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth == 1) {
        if (len == 13 && memcmp(key, "nextPageToken", 13) == 0)
          self->lastKey = LastKey::NEXT_PAGE_TOKEN;
        else if (len == 5 && memcmp(key, "files", 5) == 0)
          self->lastKey = LastKey::FILES;
        else
          self->lastKey = LastKey::NONE;
      }
      break;
    case Position::IN_FILE_OBJECT:
      if (self->fileDepth == 1) {
        if (len == 2 && memcmp(key, "id", 2) == 0)
          self->lastKey = LastKey::FILE_ID;
        else if (len == 4 && memcmp(key, "name", 4) == 0)
          self->lastKey = LastKey::FILE_NAME;
        else if (len == 8 && memcmp(key, "mimeType", 8) == 0)
          self->lastKey = LastKey::FILE_MIME;
        else if (len == 4 && memcmp(key, "size", 4) == 0)
          self->lastKey = LastKey::FILE_SIZE;
        else
          self->lastKey = LastKey::NONE;
      }
      break;
    default:
      break;
  }
}

void DriveListJsonParser::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<DriveListJsonParser*>(ctx);

  switch (self->lastKey) {
    case LastKey::NEXT_PAGE_TOKEN:
      if (self->position == Position::TOP_LEVEL && self->depth == 1)
        safeCopy(self->nextPageToken, sizeof(self->nextPageToken), value, len);
      break;
    case LastKey::FILE_ID:
      if (self->position == Position::IN_FILE_OBJECT && self->fileDepth == 1)
        safeCopy(self->current.id, sizeof(self->current.id), value, len);
      break;
    case LastKey::FILE_NAME:
      if (self->position == Position::IN_FILE_OBJECT && self->fileDepth == 1)
        safeCopy(self->current.name, sizeof(self->current.name), value, len);
      break;
    case LastKey::FILE_MIME:
      if (self->position == Position::IN_FILE_OBJECT && self->fileDepth == 1)
        safeCopy(self->current.mimeType, sizeof(self->current.mimeType), value, len);
      break;
    case LastKey::FILE_SIZE:
      // Drive API v3 sends "size" as a JSON string, e.g. "size": "12345"
      if (self->position == Position::IN_FILE_OBJECT && self->fileDepth == 1)
        self->current.size = static_cast<uint32_t>(strtoul(value, nullptr, 10));
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void DriveListJsonParser::sOnNumber(void* ctx, const char* value, size_t /*len*/) {
  auto* self = static_cast<DriveListJsonParser*>(ctx);

  // Be liberal: accept a bare-number size too, in case the API ever changes
  if (self->lastKey == LastKey::FILE_SIZE && self->position == Position::IN_FILE_OBJECT && self->fileDepth == 1) {
    self->current.size = static_cast<uint32_t>(strtoul(value, nullptr, 10));
  }
  self->lastKey = LastKey::NONE;
}

void DriveListJsonParser::sOnBool(void* ctx, bool /*value*/) {
  static_cast<DriveListJsonParser*>(ctx)->lastKey = LastKey::NONE;
}

void DriveListJsonParser::sOnNull(void* ctx) { static_cast<DriveListJsonParser*>(ctx)->lastKey = LastKey::NONE; }

void DriveListJsonParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<DriveListJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      self->depth++;
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_FILES_ARRAY:
      self->position = Position::IN_FILE_OBJECT;
      self->fileDepth = 1;
      self->current.id[0] = '\0';
      self->current.name[0] = '\0';
      self->current.mimeType[0] = '\0';
      self->current.size = 0;
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_FILE_OBJECT:
      self->fileDepth++;
      self->lastKey = LastKey::NONE;
      break;
  }
}

void DriveListJsonParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<DriveListJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_FILE_OBJECT:
      self->fileDepth--;
      if (self->fileDepth == 0) {
        self->commitFile();
        self->position = Position::IN_FILES_ARRAY;
      }
      self->lastKey = LastKey::NONE;
      break;
    default:
      break;
  }
}

void DriveListJsonParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<DriveListJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->lastKey == LastKey::FILES && self->depth == 1) {
        self->position = Position::IN_FILES_ARRAY;
      } else {
        self->depth++;
      }
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_FILE_OBJECT:
      self->fileDepth++;
      self->lastKey = LastKey::NONE;
      break;
    default:
      break;
  }
}

void DriveListJsonParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<DriveListJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_FILES_ARRAY:
      self->position = Position::TOP_LEVEL;
      break;
    case Position::IN_FILE_OBJECT:
      self->fileDepth--;
      self->lastKey = LastKey::NONE;
      break;
  }
}

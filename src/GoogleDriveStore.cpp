#include "GoogleDriveStore.h"

#include <ArduinoJson.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <algorithm>
#include <cctype>

GoogleDriveStore GoogleDriveStore::instance;

namespace {
constexpr char GDRIVE_FILE_JSON[] = "/.crosspoint/gdrive.json";

std::string caseFoldAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}
}  // namespace

bool GoogleDriveStore::saveToFile() const {
  if (!dirty) {
    return true;
  }
  Storage.mkdir("/.crosspoint");

  JsonDocument doc;
  doc["folderId"] = folderId;
  doc["apiKey_obf"] = obfuscation::obfuscateToBase64(apiKey);
  doc["localFolder"] = localFolder;

  String json;
  serializeJson(doc, json);
  const bool ok = Storage.writeFile(GDRIVE_FILE_JSON, json);
  if (ok) {
    dirty = false;
  }
  return ok;
}

bool GoogleDriveStore::loadFromFile() {
  if (!Storage.exists(GDRIVE_FILE_JSON)) {
    LOG_DBG("GDRV", "No config file found");
    return false;
  }

  const String json = Storage.readFile(GDRIVE_FILE_JSON);
  if (json.isEmpty()) {
    return false;
  }

  JsonDocument doc;
  const auto error = deserializeJson(doc, json.c_str());
  if (error) {
    LOG_ERR("GDRV", "JSON parse error: %s", error.c_str());
    return false;
  }

  folderId = doc["folderId"] | std::string("");
  bool ok = false;
  apiKey = obfuscation::deobfuscateFromBase64(doc["apiKey_obf"] | "", &ok);
  if (!ok) {
    apiKey.clear();
  }
  const std::string storedLocalFolder = doc["localFolder"] | std::string("");
  std::string normalized;
  std::string validationError;
  if (!storedLocalFolder.empty() && validateLocalFolder(storedLocalFolder, normalized, validationError)) {
    localFolder = std::move(normalized);
  } else {
    localFolder.clear();
    if (!storedLocalFolder.empty()) {
      LOG_ERR("GDRV", "Stored local folder is invalid: %s", validationError.c_str());
    }
  }
  dirty = false;

  LOG_DBG("GDRV", "Loaded config (folderId %s, local folder %s)", folderId.empty() ? "empty" : "set",
          localFolder.empty() ? "empty" : "set");
  return true;
}

void GoogleDriveStore::setFolderId(const std::string& id) {
  if (id == folderId) {
    return;
  }
  folderId = id;
  dirty = true;
  LOG_DBG("GDRV", "Set folder ID");
}

void GoogleDriveStore::setApiKey(const std::string& key) {
  if (key == apiKey) {
    return;
  }
  apiKey = key;
  dirty = true;
  LOG_DBG("GDRV", "Set API key");
}

void GoogleDriveStore::setLocalFolder(const std::string& path) {
  if (path == localFolder) {
    return;
  }
  localFolder = path;
  dirty = true;
  LOG_DBG("GDRV", "Set local folder: %s", localFolder.c_str());
}

bool GoogleDriveStore::validateLocalFolder(const std::string& input, std::string& normalized, std::string& error) {
  normalized.clear();
  error.clear();
  if (input.empty() || input.front() != '/') {
    error = "Google Drive local folder must be an absolute SD-card path";
    return false;
  }
  if (input.find('\\') != std::string::npos) {
    error = "Backslashes are not allowed in the local folder path";
    return false;
  }
  size_t componentStart = 1;
  while (componentStart <= input.size()) {
    const size_t componentEnd = input.find('/', componentStart);
    const std::string component = input.substr(componentStart, componentEnd - componentStart);
    if (component == "." || component == "..") {
      error = "Dot path components are not allowed in the local folder path";
      return false;
    }
    if (componentEnd == std::string::npos) break;
    componentStart = componentEnd + 1;
  }

  const std::string relative = FsHelpers::normalisePath(input);
  if (relative.empty()) {
    error = "The SD-card root cannot be used as a Google Drive mirror";
    return false;
  }
  normalized = "/" + relative;
  if (normalized.size() > 240) {
    error = "The selected local folder path is too long";
    return false;
  }
  const std::string folded = caseFoldAscii(normalized);
  if (folded == "/.crosspoint" || folded.rfind("/.crosspoint/", 0) == 0 || folded == "/.sleep" ||
      folded.rfind("/.sleep/", 0) == 0 || folded == "/system volume information" ||
      folded.rfind("/system volume information/", 0) == 0 || folded == "/xtcache" ||
      folded.rfind("/xtcache/", 0) == 0) {
    error = "The selected local folder is reserved by the system";
    return false;
  }

  HalFile folder = Storage.open(normalized.c_str());
  if (!folder || !folder.isDirectory()) {
    if (folder) folder.close();
    error = "The selected local folder does not exist or is not a directory";
    return false;
  }
  folder.close();
  return true;
}

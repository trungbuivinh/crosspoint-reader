#include "GoogleDriveStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

GoogleDriveStore GoogleDriveStore::instance;

namespace {
constexpr char GDRIVE_FILE_JSON[] = "/.crosspoint/gdrive.json";
}  // namespace

bool GoogleDriveStore::saveToFile() const {
  if (!dirty) {
    return true;
  }
  Storage.mkdir("/.crosspoint");

  JsonDocument doc;
  doc["folderId"] = folderId;
  doc["apiKey_obf"] = obfuscation::obfuscateToBase64(apiKey);

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
  dirty = false;

  LOG_DBG("GDRV", "Loaded config (folderId %s)", folderId.empty() ? "empty" : "set");
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

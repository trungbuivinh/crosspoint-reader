#pragma once
#include <string>

/**
 * Singleton class for storing Google Drive sync configuration on the SD card.
 * The API key is XOR-obfuscated with the device's unique hardware MAC address
 * and base64-encoded before writing to JSON (not cryptographically secure,
 * but prevents casual reading and ties the key to the specific device).
 */
class GoogleDriveStore {
 private:
  static GoogleDriveStore instance;
  std::string folderId;
  std::string apiKey;
  // Set by the setters on a real value change; saveToFile() skips the SD
  // write when clear, so callers can save unconditionally (write throttling).
  mutable bool dirty = false;

  GoogleDriveStore() = default;

 public:
  GoogleDriveStore(const GoogleDriveStore&) = delete;
  GoogleDriveStore& operator=(const GoogleDriveStore&) = delete;

  static GoogleDriveStore& getInstance() { return instance; }

  bool saveToFile() const;
  bool loadFromFile();

  const std::string& getFolderId() const { return folderId; }
  void setFolderId(const std::string& id);

  const std::string& getApiKey() const { return apiKey; }
  void setApiKey(const std::string& key);

  bool isConfigured() const { return !folderId.empty() && !apiKey.empty(); }
};

// Helper macro to access the Google Drive store
#define GDRIVE_STORE GoogleDriveStore::getInstance()

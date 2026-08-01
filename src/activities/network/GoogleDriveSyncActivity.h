#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "network/GoogleDriveClient.h"

/**
 * GoogleDriveSyncActivity pulls books from a link-shared Google Drive folder
 * configured via web settings (folder ID + API key, see GoogleDriveStore).
 * One-button sync-all: lists the folder, then downloads every supported file
 * that is missing locally or whose local size differs.
 */
class GoogleDriveSyncActivity final : public Activity {
 public:
  enum class SyncState { NOT_CONFIGURED, CHECK_WIFI, WIFI_SELECTION, LISTING, READY, SYNCING, DONE, ERROR };

  explicit GoogleDriveSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("GoogleDriveSync", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  SyncState state = SyncState::CHECK_WIFI;

  std::vector<DriveFileEntry> files;  // filtered folder listing
  std::vector<uint16_t> pending;      // indices into files needing download
  size_t currentIndex = 0;            // position in pending while SYNCING
  size_t downloadedCount = 0;
  size_t failedCount = 0;

  // Per-file progress for the render loop
  std::string currentName;
  size_t fileProgress = 0;
  size_t fileTotal = 0;

  bool cancelRequested = false;
  std::string errorMessage;

  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void startListing();
  void buildPendingList();
  void syncNext();
  static std::string localPathFor(const DriveFileEntry& file);

  bool preventAutoSleep() override { return true; }
};

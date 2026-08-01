#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "network/GoogleDriveClient.h"
#include "network/GoogleDriveSyncPlan.h"

/** Pulls a shared Google Drive EPUB tree into a dedicated SD-card mirror root. */
class GoogleDriveSyncActivity final : public Activity {
 public:
  enum class SyncState {
    NOT_CONFIGURED,
    CHECK_WIFI,
    WIFI_SELECTION,
    LISTING,
    ANALYZING,
    READY,
    SYNCING,
    FINALIZING,
    DONE,
    INCOMPLETE,
    ERROR,
  };

  explicit GoogleDriveSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("GoogleDriveSync", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class SyncPhase {
    RENAME_PATHS,
    PREPARE_CONFLICTS,
    CREATE_DIRECTORIES,
    DOWNLOAD_EPUBS,
    DELETE_FILES,
    DELETE_DIRECTORIES
  };

  struct ConflictBackup {
    std::string finalPath;
    std::string backupPath;
    LocalDriveNodeType localType;
  };

  SyncState state = SyncState::CHECK_WIFI;
  SyncPhase phase = SyncPhase::PREPARE_CONFLICTS;
  DriveTree remoteTree;
  std::vector<LocalDriveNode> localTree;
  GoogleDriveSyncPlan plan;
  std::vector<ConflictBackup> conflictBackups;

  size_t phaseIndex = 0;
  size_t completedActions = 0;
  size_t downloadedCount = 0;
  size_t deletedFileCount = 0;
  size_t deletedDirectoryCount = 0;
  size_t failedCount = 0;

  std::string currentName;
  size_t fileProgress = 0;
  size_t fileTotal = 0;
  bool cancelRequested = false;
  std::string errorMessage;

  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void startListing();
  bool buildPlan();
  bool recoverArtifacts();
  bool scanLocalTree();
  bool calculateFileMd5(const std::string& path, std::string& out) const;

  void requestStartSync();
  void startSync();
  void processNextAction();
  bool prepareConflicts();
  bool rollbackConflicts();
  bool removeConflictBackups();
  bool renameNextPath();
  bool createNextDirectory();
  bool downloadNextEpub();
  bool deleteNextFile();
  bool deleteNextDirectory();
  void finishIncomplete(const std::string& message);
  void finishSuccess();

  std::string absolutePath(const std::string& relativePath) const;
  bool removePathRecursively(const std::string& path, bool clearMetadata);
  static std::string driveErrorMessage(GoogleDriveClient::DriveTreeResult result);

  bool preventAutoSleep() override { return true; }
};

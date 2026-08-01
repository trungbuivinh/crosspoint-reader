#include "GoogleDriveSyncActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <MD5Builder.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <utility>

#include "GoogleDriveStore.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "util/BookCacheUtils.h"

namespace {

constexpr size_t FILE_NAME_BUFFER_SIZE = 500;
constexpr char PART_SUFFIX[] = ".gdrive.part";
constexpr char BACKUP_SUFFIX[] = ".gdrive.bak";
constexpr char CASE_SUFFIX[] = ".gdrive.case";

std::string caseFoldAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool endsWithCaseInsensitive(const std::string& value, const char* suffix) {
  const size_t suffixLength = strlen(suffix);
  if (value.size() < suffixLength) return false;
  return caseFoldAscii(value.substr(value.size() - suffixLength)) == caseFoldAscii(suffix);
}

}  // namespace

void GoogleDriveSyncActivity::onEnter() {
  Activity::onEnter();

  state = SyncState::CHECK_WIFI;
  phase = SyncPhase::RENAME_PATHS;
  remoteTree.nodes.clear();
  localTree.clear();
  plan = {};
  conflictBackups.clear();
  phaseIndex = 0;
  completedActions = 0;
  downloadedCount = 0;
  deletedFileCount = 0;
  deletedDirectoryCount = 0;
  failedCount = 0;
  currentName.clear();
  fileProgress = 0;
  fileTotal = 0;
  cancelRequested = false;
  errorMessage.clear();

  if (!GDRIVE_STORE.isConfigured()) {
    state = SyncState::NOT_CONFIGURED;
    requestUpdate();
    return;
  }

  requestUpdate();
  checkAndConnectWifi();
}

void GoogleDriveSyncActivity::onExit() {
  Activity::onExit();
  remoteTree.nodes.clear();
  localTree.clear();
  plan = {};
  conflictBackups.clear();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void GoogleDriveSyncActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    startListing();
    return;
  }
  launchWifiSelection();
}

void GoogleDriveSyncActivity::launchWifiSelection() {
  state = SyncState::WIFI_SELECTION;
  requestUpdate();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void GoogleDriveSyncActivity::onWifiSelectionComplete(const bool connected) {
  if (connected) {
    startListing();
  } else {
    state = SyncState::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
  }
}

std::string GoogleDriveSyncActivity::driveErrorMessage(const GoogleDriveClient::DriveTreeResult result) {
  switch (result) {
    case GoogleDriveClient::DriveTreeResult::HTTP_ERROR:
      return "Failed to fetch the Drive tree";
    case GoogleDriveClient::DriveTreeResult::PARSE_ERROR:
      return "Invalid response while listing Drive";
    case GoogleDriveClient::DriveTreeResult::TOO_MANY_ENTRIES:
      return "Drive tree exceeds the 300-entry limit";
    case GoogleDriveClient::DriveTreeResult::TOO_DEEP:
      return "Drive tree exceeds the 16-level limit";
    case GoogleDriveClient::DriveTreeResult::PATH_TOO_LONG:
      return "A Drive path is too long";
    case GoogleDriveClient::DriveTreeResult::INVALID_NAME:
      return "A Drive name cannot be represented on the SD card";
    case GoogleDriveClient::DriveTreeResult::NAME_COLLISION:
      return "Drive contains colliding names in one folder";
    case GoogleDriveClient::DriveTreeResult::CYCLE_DETECTED:
      return "Drive folder cycle detected";
    case GoogleDriveClient::DriveTreeResult::OK:
      break;
  }
  return "Failed to list Drive folder";
}

void GoogleDriveSyncActivity::startListing() {
  state = SyncState::LISTING;
  requestUpdate(true);

  const auto result = GoogleDriveClient::listTree(GDRIVE_STORE.getFolderId(), GDRIVE_STORE.getApiKey(), remoteTree);
  if (result != GoogleDriveClient::DriveTreeResult::OK) {
    state = SyncState::ERROR;
    errorMessage = driveErrorMessage(result);
    requestUpdate();
    return;
  }

  state = SyncState::ANALYZING;
  requestUpdate(true);
  if (!buildPlan()) {
    state = SyncState::ERROR;
    if (errorMessage.empty()) errorMessage = "Failed to analyze the local mirror";
    requestUpdate();
    return;
  }

  state = SyncState::READY;
  requestUpdate();
}

std::string GoogleDriveSyncActivity::absolutePath(const std::string& relativePath) const {
  if (relativePath.empty()) return GDRIVE_STORE.getLocalFolder();
  return GDRIVE_STORE.getLocalFolder() + "/" + relativePath;
}

bool GoogleDriveSyncActivity::calculateFileMd5(const std::string& path, std::string& out) const {
  HalFile file;
  if (!Storage.openFileForRead("GDRV", path, file)) {
    return false;
  }

  MD5Builder md5;
  md5.begin();
  uint8_t buffer[4096];
  while (file.available() > 0) {
    const int read = file.read(buffer, sizeof(buffer));
    if (read <= 0) {
      file.close();
      return false;
    }
    md5.add(buffer, static_cast<size_t>(read));
    yield();
    esp_task_wdt_reset();
  }
  file.close();
  md5.calculate();
  out = md5.toString().c_str();
  return true;
}

bool GoogleDriveSyncActivity::removePathRecursively(const std::string& path, const bool clearMetadata) {
  HalFile root = Storage.open(path.c_str());
  if (!root) return true;
  if (!root.isDirectory()) {
    root.close();
    if (clearMetadata && FsHelpers::hasEpubExtension(path)) {
      clearBookCache(path);
      RECENT_BOOKS.removeByPath(path);
    }
    return Storage.remove(path.c_str());
  }
  root.close();

  std::vector<std::pair<std::string, bool>> stack;
  stack.push_back({path, false});
  char name[FILE_NAME_BUFFER_SIZE];
  while (!stack.empty()) {
    auto [current, postOrder] = std::move(stack.back());
    stack.pop_back();
    if (postOrder) {
      if (!Storage.rmdir(current.c_str())) return false;
      continue;
    }

    HalFile dir = Storage.open(current.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      return false;
    }
    stack.push_back({current, true});
    dir.rewindDirectory();
    for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      entry.getName(name, sizeof(name));
      if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
      const bool isDirectory = entry.isDirectory();
      entry.close();
      const std::string child = current + "/" + name;
      if (isDirectory) {
        stack.push_back({child, false});
      } else {
        if (clearMetadata && FsHelpers::hasEpubExtension(child)) {
          clearBookCache(child);
          RECENT_BOOKS.removeByPath(child);
        }
        if (!Storage.remove(child.c_str())) return false;
      }
    }
    dir.close();
    yield();
    esp_task_wdt_reset();
  }
  return true;
}

bool GoogleDriveSyncActivity::recoverArtifacts() {
  struct RecoverableArtifact {
    std::string path;
    size_t suffixLength;
  };
  std::vector<RecoverableArtifact> artifacts;
  std::vector<std::string> directories{GDRIVE_STORE.getLocalFolder()};
  char name[FILE_NAME_BUFFER_SIZE];

  while (!directories.empty()) {
    const std::string current = std::move(directories.back());
    directories.pop_back();
    HalFile dir = Storage.open(current.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      errorMessage = "Failed to scan sync artifacts";
      return false;
    }
    dir.rewindDirectory();
    for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      entry.getName(name, sizeof(name));
      if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
      const std::string child = current + "/" + name;
      const bool isDirectory = entry.isDirectory();
      entry.close();
      if (endsWithCaseInsensitive(name, BACKUP_SUFFIX)) {
        artifacts.push_back({child, strlen(BACKUP_SUFFIX)});
      } else if (endsWithCaseInsensitive(name, CASE_SUFFIX)) {
        artifacts.push_back({child, strlen(CASE_SUFFIX)});
      } else if (isDirectory) {
        directories.push_back(child);
      }
    }
    dir.close();
  }

  for (const auto& artifact : artifacts) {
    const std::string finalPath = artifact.path.substr(0, artifact.path.size() - artifact.suffixLength);
    // If the final path exists, leave the stale artifact in the local snapshot so
    // it is shown in the destructive plan and removed only after confirmation.
    if (!Storage.exists(finalPath.c_str()) && !Storage.rename(artifact.path.c_str(), finalPath.c_str())) {
      errorMessage = "Failed to restore a sync backup";
      return false;
    }
  }
  return true;
}

bool GoogleDriveSyncActivity::scanLocalTree() {
  localTree.clear();
  std::vector<std::pair<std::string, std::string>> directories{{GDRIVE_STORE.getLocalFolder(), ""}};
  char name[FILE_NAME_BUFFER_SIZE];

  while (!directories.empty()) {
    auto [absoluteDirectory, relativeDirectory] = std::move(directories.back());
    directories.pop_back();
    HalFile dir = Storage.open(absoluteDirectory.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      errorMessage = "Failed to scan the local mirror";
      return false;
    }
    dir.rewindDirectory();
    for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      entry.getName(name, sizeof(name));
      if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
      const std::string relativePath = relativeDirectory.empty() ? name : relativeDirectory + "/" + name;
      const std::string fullPath = absoluteDirectory + "/" + name;
      if (fullPath.size() + strlen(PART_SUFFIX) > GoogleDriveClient::MAX_LOCAL_PATH_BYTES) {
        entry.close();
        dir.close();
        errorMessage = "A local mirror path is too long";
        return false;
      }
      if (localTree.size() >= GoogleDriveClient::MAX_TREE_ENTRIES) {
        entry.close();
        dir.close();
        errorMessage = "Local mirror exceeds the 300-entry limit";
        return false;
      }

      const bool isDirectory = entry.isDirectory();
      const uint32_t size = isDirectory ? 0 : static_cast<uint32_t>(entry.size());
      entry.close();
      if (isDirectory) {
        localTree.push_back({LocalDriveNodeType::DIRECTORY, relativePath, 0, ""});
        directories.push_back({fullPath, relativePath});
        continue;
      }

      std::string md5;
      const std::string key = caseFoldAscii(relativePath);
      const auto remote = std::find_if(remoteTree.nodes.begin(), remoteTree.nodes.end(), [&](const DriveNode& node) {
        return node.type == DriveNodeType::EPUB && caseFoldAscii(node.relativePath) == key;
      });
      if (remote != remoteTree.nodes.end() && remote->size == size && !remote->md5Checksum.empty() &&
          !calculateFileMd5(fullPath, md5)) {
        dir.close();
        errorMessage = "Failed to hash a local EPUB";
        return false;
      }
      localTree.push_back({LocalDriveNodeType::FILE, relativePath, size, std::move(md5)});
    }
    dir.close();
    yield();
    esp_task_wdt_reset();
  }
  return true;
}

bool GoogleDriveSyncActivity::buildPlan() {
  const bool hasOverlongPath = std::any_of(remoteTree.nodes.begin(), remoteTree.nodes.end(), [this](const auto& node) {
    return absolutePath(node.relativePath).size() + strlen(PART_SUFFIX) > GoogleDriveClient::MAX_LOCAL_PATH_BYTES;
  });
  if (hasOverlongPath) {
    errorMessage = "A Drive path is too long for the selected local folder";
    return false;
  }
  if (!recoverArtifacts() || !scanLocalTree()) return false;
  const SyncPlanError result = buildGoogleDriveSyncPlan(remoteTree, localTree, plan);
  if (result != SyncPlanError::NONE) {
    errorMessage = "Unable to build an exact mirror plan";
    return false;
  }
  return true;
}

void GoogleDriveSyncActivity::requestStartSync() {
  if (plan.destructiveFileCount() == 0 && plan.destructiveDirectoryCount() == 0) {
    startSync();
    return;
  }

  char body[128];
  snprintf(body, sizeof(body), tr(STR_GDRIVE_DELETE_CONFIRM_FORMAT), static_cast<int>(plan.destructiveFileCount()),
           static_cast<int>(plan.destructiveDirectoryCount()));
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_GDRIVE_DELETE_WARNING), body),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) startSync();
      });
}

void GoogleDriveSyncActivity::startSync() {
  state = SyncState::SYNCING;
  phase = SyncPhase::RENAME_PATHS;
  phaseIndex = 0;
  completedActions = 0;
  cancelRequested = false;
  currentName = tr(STR_GDRIVE_PREPARING);
  requestUpdate(true);
}

bool GoogleDriveSyncActivity::renameNextPath() {
  const auto& rename = plan.caseRenames[phaseIndex];
  currentName = rename.targetRelativePath;
  fileProgress = fileTotal = 0;
  requestUpdate(true);

  const std::string source = absolutePath(rename.sourceRelativePath);
  const std::string target = absolutePath(rename.targetRelativePath);
  const std::string temporary = source + CASE_SUFFIX;
  if (Storage.exists(temporary.c_str()) && !removePathRecursively(temporary, false)) return false;
  if (!Storage.rename(source.c_str(), temporary.c_str())) return false;
  if (!Storage.rename(temporary.c_str(), target.c_str())) {
    Storage.rename(temporary.c_str(), source.c_str());
    return false;
  }

  if (rename.localType == LocalDriveNodeType::FILE) {
    if (FsHelpers::hasEpubExtension(source)) clearBookCache(source);
    RECENT_BOOKS.removeByPath(source);
  } else {
    const std::string prefix = rename.sourceRelativePath + "/";
    for (const auto& local : localTree) {
      if (local.type != LocalDriveNodeType::FILE || local.relativePath.rfind(prefix, 0) != 0) continue;
      const std::string originalPath = absolutePath(local.relativePath);
      if (FsHelpers::hasEpubExtension(originalPath)) clearBookCache(originalPath);
      RECENT_BOOKS.removeByPath(originalPath);
    }
  }
  return true;
}

bool GoogleDriveSyncActivity::prepareConflicts() {
  conflictBackups.clear();
  for (const auto& conflict : plan.typeConflicts) {
    const std::string finalPath = absolutePath(conflict.relativePath);
    const std::string backupPath = finalPath + BACKUP_SUFFIX;
    currentName = conflict.relativePath;
    requestUpdate(true);
    if (Storage.exists(backupPath.c_str()) && !removePathRecursively(backupPath, false)) {
      rollbackConflicts();
      return false;
    }
    if (!Storage.rename(finalPath.c_str(), backupPath.c_str())) {
      rollbackConflicts();
      return false;
    }
    conflictBackups.push_back({finalPath, backupPath, conflict.localType});
  }
  return true;
}

bool GoogleDriveSyncActivity::rollbackConflicts() {
  bool ok = true;
  for (auto it = conflictBackups.rbegin(); it != conflictBackups.rend(); ++it) {
    if (Storage.exists(it->finalPath.c_str()) && !removePathRecursively(it->finalPath, false)) ok = false;
    if (Storage.exists(it->backupPath.c_str()) && !Storage.rename(it->backupPath.c_str(), it->finalPath.c_str())) {
      ok = false;
    }
  }
  conflictBackups.clear();
  return ok;
}

bool GoogleDriveSyncActivity::removeConflictBackups() {
  bool ok = true;
  for (const auto& backup : conflictBackups) {
    const std::string relative = backup.finalPath.substr(GDRIVE_STORE.getLocalFolder().size() + 1);
    for (const auto& local : localTree) {
      if (local.relativePath.size() > relative.size() &&
          local.relativePath.compare(0, relative.size(), relative) == 0 && local.relativePath[relative.size()] == '/') {
        const std::string originalPath = absolutePath(local.relativePath);
        if (local.type == LocalDriveNodeType::FILE) {
          if (FsHelpers::hasEpubExtension(originalPath)) clearBookCache(originalPath);
          RECENT_BOOKS.removeByPath(originalPath);
          deletedFileCount++;
        } else {
          deletedDirectoryCount++;
        }
      }
    }
    if (backup.localType == LocalDriveNodeType::FILE) {
      if (FsHelpers::hasEpubExtension(backup.finalPath)) clearBookCache(backup.finalPath);
      RECENT_BOOKS.removeByPath(backup.finalPath);
    }
    if (!removePathRecursively(backup.backupPath, false)) ok = false;
    if (backup.localType == LocalDriveNodeType::DIRECTORY)
      deletedDirectoryCount++;
    else
      deletedFileCount++;
  }
  conflictBackups.clear();
  return ok;
}

bool GoogleDriveSyncActivity::createNextDirectory() {
  const std::string& relative = plan.directoriesToCreate[phaseIndex];
  currentName = relative;
  fileProgress = fileTotal = 0;
  requestUpdate(true);
  const std::string path = absolutePath(relative);
  if (Storage.exists(path.c_str())) {
    HalFile existing = Storage.open(path.c_str());
    const bool isDirectory = existing && existing.isDirectory();
    if (existing) existing.close();
    return isDirectory;
  }
  return Storage.mkdir(path.c_str());
}

bool GoogleDriveSyncActivity::downloadNextEpub() {
  const DriveNode& file = remoteTree.nodes[plan.epubsToDownload[phaseIndex]];
  currentName = file.relativePath;
  fileProgress = 0;
  fileTotal = file.size;
  requestUpdate(true);

  const std::string destination = absolutePath(file.relativePath);
  const std::string partPath = destination + PART_SUFFIX;
  const std::string backupPath = destination + BACKUP_SUFFIX;
  if (Storage.exists(partPath.c_str())) removePathRecursively(partPath, false);

  const auto result = HttpDownloader::downloadToFile(
      GoogleDriveClient::downloadUrl(file.id, GDRIVE_STORE.getApiKey()), partPath,
      [this](const size_t downloaded, const size_t total) {
        fileProgress = downloaded;
        if (total > 0) fileTotal = total;
        mappedInput.update();
        if (mappedInput.wasPressed(MappedInputManager::Button::Back)) cancelRequested = true;
        requestUpdate(true);
      },
      &cancelRequested);
  if (result != HttpDownloader::OK) return false;

  HalFile downloaded;
  if (!Storage.openFileForRead("GDRV", partPath, downloaded)) {
    Storage.remove(partPath.c_str());
    return false;
  }
  const uint32_t downloadedSize = static_cast<uint32_t>(downloaded.size());
  downloaded.close();
  if (downloadedSize != file.size) {
    Storage.remove(partPath.c_str());
    return false;
  }
  if (!file.md5Checksum.empty()) {
    std::string md5;
    if (!calculateFileMd5(partPath, md5) || caseFoldAscii(md5) != caseFoldAscii(file.md5Checksum)) {
      Storage.remove(partPath.c_str());
      return false;
    }
  }

  bool hadExisting = Storage.exists(destination.c_str());
  if (hadExisting) {
    if (Storage.exists(backupPath.c_str()) && !removePathRecursively(backupPath, false)) {
      Storage.remove(partPath.c_str());
      return false;
    }
    if (!Storage.rename(destination.c_str(), backupPath.c_str())) {
      Storage.remove(partPath.c_str());
      return false;
    }
  }
  if (!Storage.rename(partPath.c_str(), destination.c_str())) {
    if (hadExisting) Storage.rename(backupPath.c_str(), destination.c_str());
    Storage.remove(partPath.c_str());
    return false;
  }
  if (hadExisting) {
    if (!Storage.remove(backupPath.c_str())) return false;
    clearBookCache(destination);
    RECENT_BOOKS.removeByPath(destination);
  }
  downloadedCount++;
  return true;
}

bool GoogleDriveSyncActivity::deleteNextFile() {
  const std::string& relative = plan.filesToDelete[phaseIndex];
  currentName = relative;
  fileProgress = fileTotal = 0;
  requestUpdate(true);
  const std::string path = absolutePath(relative);
  if (FsHelpers::hasEpubExtension(path)) {
    clearBookCache(path);
    RECENT_BOOKS.removeByPath(path);
  }
  if (!Storage.exists(path.c_str()) || Storage.remove(path.c_str())) {
    deletedFileCount++;
    return true;
  }
  return false;
}

bool GoogleDriveSyncActivity::deleteNextDirectory() {
  const std::string& relative = plan.directoriesToDelete[phaseIndex];
  currentName = relative;
  fileProgress = fileTotal = 0;
  requestUpdate(true);
  const std::string path = absolutePath(relative);
  if (!Storage.exists(path.c_str()) || Storage.rmdir(path.c_str())) {
    deletedDirectoryCount++;
    return true;
  }
  return false;
}

void GoogleDriveSyncActivity::finishIncomplete(const std::string& message) {
  if (!rollbackConflicts()) failedCount++;
  state = SyncState::INCOMPLETE;
  errorMessage = message;
  fileProgress = fileTotal = 0;
  requestUpdate();
}

void GoogleDriveSyncActivity::finishSuccess() {
  if (RECENT_BOOKS.pruneMissing()) RECENT_BOOKS.saveToFile();
  state = failedCount == 0 ? SyncState::DONE : SyncState::INCOMPLETE;
  if (failedCount != 0 && errorMessage.empty()) errorMessage = tr(STR_GDRIVE_INCOMPLETE);
  fileProgress = fileTotal = 0;
  requestUpdate();
}

void GoogleDriveSyncActivity::processNextAction() {
  switch (phase) {
    case SyncPhase::RENAME_PATHS:
      if (cancelRequested) {
        finishIncomplete(tr(STR_SYNC_CANCELLED));
        return;
      }
      if (phaseIndex < plan.caseRenames.size()) {
        if (!renameNextPath()) {
          failedCount++;
          finishIncomplete("Failed to match Drive path casing");
          return;
        }
        phaseIndex++;
        completedActions++;
        return;
      }
      phase = SyncPhase::PREPARE_CONFLICTS;
      phaseIndex = 0;
      return;

    case SyncPhase::PREPARE_CONFLICTS:
      if (cancelRequested) {
        finishIncomplete(tr(STR_SYNC_CANCELLED));
        return;
      }
      if (!prepareConflicts()) {
        failedCount++;
        finishIncomplete("Failed to prepare a path replacement");
        return;
      }
      phase = SyncPhase::CREATE_DIRECTORIES;
      phaseIndex = 0;
      return;

    case SyncPhase::CREATE_DIRECTORIES:
      if (cancelRequested) {
        finishIncomplete(tr(STR_SYNC_CANCELLED));
        return;
      }
      if (phaseIndex < plan.directoriesToCreate.size()) {
        if (!createNextDirectory()) {
          failedCount++;
          finishIncomplete("Failed to create a mirror directory");
          return;
        }
        phaseIndex++;
        completedActions++;
        return;
      }
      phase = SyncPhase::DOWNLOAD_EPUBS;
      phaseIndex = 0;
      return;

    case SyncPhase::DOWNLOAD_EPUBS:
      if (phaseIndex < plan.epubsToDownload.size()) {
        if (!downloadNextEpub() && !cancelRequested) failedCount++;
        phaseIndex++;
        completedActions++;
        return;
      }
      if (cancelRequested || failedCount > 0) {
        finishIncomplete(cancelRequested ? tr(STR_SYNC_CANCELLED) : tr(STR_GDRIVE_INCOMPLETE));
        return;
      }
      state = SyncState::FINALIZING;
      currentName = tr(STR_GDRIVE_FINALIZING);
      requestUpdate(true);
      if (!removeConflictBackups()) failedCount++;
      phase = SyncPhase::DELETE_FILES;
      phaseIndex = 0;
      return;

    case SyncPhase::DELETE_FILES:
      if (phaseIndex < plan.filesToDelete.size()) {
        if (!deleteNextFile()) failedCount++;
        phaseIndex++;
        completedActions++;
        return;
      }
      phase = SyncPhase::DELETE_DIRECTORIES;
      phaseIndex = 0;
      return;

    case SyncPhase::DELETE_DIRECTORIES:
      if (phaseIndex < plan.directoriesToDelete.size()) {
        if (!deleteNextDirectory()) failedCount++;
        phaseIndex++;
        completedActions++;
        return;
      }
      finishSuccess();
      return;
  }
}

void GoogleDriveSyncActivity::loop() {
  if (state == SyncState::WIFI_SELECTION || state == SyncState::CHECK_WIFI || state == SyncState::LISTING ||
      state == SyncState::ANALYZING) {
    return;
  }

  if (state == SyncState::SYNCING || state == SyncState::FINALIZING) {
    if (state == SyncState::SYNCING && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      cancelRequested = true;
    }
    processNextAction();
    return;
  }

  if (state == SyncState::READY) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && plan.actionCount() > 0) {
      requestStartSync();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  if (state == SyncState::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0))
        startListing();
      else
        launchWifiSelection();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    finish();
  }
}

void GoogleDriveSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  renderer.drawCenteredText(UI_12_FONT_ID, 15, tr(STR_GDRIVE), true, EpdFontFamily::BOLD);
  char buf[128];

  switch (state) {
    case SyncState::NOT_CONFIGURED:
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 55, tr(STR_GDRIVE_NOT_CONFIGURED), true,
                                EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 15, tr(STR_GDRIVE_SETUP_HINT_1));
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 15, tr(STR_GDRIVE_SETUP_HINT_2));
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 45, tr(STR_GDRIVE_SETUP_HINT_3));
      {
        const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
        GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      }
      break;

    case SyncState::CHECK_WIFI:
    case SyncState::WIFI_SELECTION:
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_CHECKING_WIFI));
      break;

    case SyncState::LISTING:
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_GDRIVE_LISTING));
      break;

    case SyncState::ANALYZING:
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_GDRIVE_ANALYZING));
      break;

    case SyncState::READY: {
      const auto target = renderer.truncatedText(UI_10_FONT_ID, GDRIVE_STORE.getLocalFolder().c_str(), pageWidth - 40);
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 55, target.c_str());
      if (plan.actionCount() == 0) {
        renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_GDRIVE_UP_TO_DATE));
        const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
        GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      } else {
        snprintf(buf, sizeof(buf), tr(STR_GDRIVE_PLAN_FORMAT), static_cast<int>(plan.directoriesToCreate.size()),
                 static_cast<int>(plan.epubsToDownload.size()), static_cast<int>(plan.caseRenames.size()));
        renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 15, buf);
        snprintf(buf, sizeof(buf), tr(STR_GDRIVE_DELETE_PLAN_FORMAT), static_cast<int>(plan.destructiveFileCount()),
                 static_cast<int>(plan.destructiveDirectoryCount()));
        renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 20, buf);
        const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_START_SYNC), "", "");
        GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      }
      break;
    }

    case SyncState::SYNCING:
    case SyncState::FINALIZING: {
      const int progressHeight = GUI.measureProgressBar(renderer);
      const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
      int y = pageHeight / 2 - progressHeight - lineHeight * 2;
      renderer.drawCenteredText(UI_10_FONT_ID, y,
                                state == SyncState::FINALIZING ? tr(STR_GDRIVE_FINALIZING) : tr(STR_DOWNLOADING));
      y += lineHeight + metrics.verticalSpacing;
      const auto title = renderer.truncatedText(UI_10_FONT_ID, currentName.c_str(), pageWidth - 40);
      renderer.drawCenteredText(UI_10_FONT_ID, y, title.c_str());
      y += lineHeight + metrics.verticalSpacing;
      if (fileTotal > 0) {
        GUI.drawProgressBar(
            renderer, Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, progressHeight},
            fileProgress, fileTotal);
        y += progressHeight + metrics.verticalSpacing;
      }
      const size_t totalActions = std::max<size_t>(plan.actionCount(), 1);
      snprintf(buf, sizeof(buf), tr(STR_GDRIVE_FILE_OF_FORMAT),
               static_cast<int>(std::min(completedActions + 1, totalActions)), static_cast<int>(totalActions));
      renderer.drawCenteredText(UI_10_FONT_ID, y, buf);
      y += lineHeight + metrics.verticalSpacing;
      GUI.drawProgressBar(
          renderer, Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, progressHeight},
          completedActions, totalActions);
      if (state == SyncState::SYNCING) {
        const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
        GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      }
      break;
    }

    case SyncState::DONE:
    case SyncState::INCOMPLETE: {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 45,
                                state == SyncState::DONE ? tr(STR_SYNC_COMPLETE) : tr(STR_GDRIVE_INCOMPLETE), true,
                                EpdFontFamily::BOLD);
      snprintf(buf, sizeof(buf), tr(STR_GDRIVE_RESULT_FORMAT), static_cast<int>(downloadedCount),
               static_cast<int>(failedCount));
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 5, buf);
      snprintf(buf, sizeof(buf), tr(STR_GDRIVE_DELETE_RESULT_FORMAT), static_cast<int>(deletedFileCount),
               static_cast<int>(deletedDirectoryCount));
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 25, buf);
      if (state == SyncState::INCOMPLETE && !errorMessage.empty()) {
        const auto message = renderer.truncatedText(SMALL_FONT_ID, errorMessage.c_str(), pageWidth - 40);
        renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 55, message.c_str());
      }
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    case SyncState::ERROR: {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_ERROR_MSG));
      const auto message = renderer.truncatedText(UI_10_FONT_ID, errorMessage.c_str(), pageWidth - 40);
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, message.c_str());
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }
  }

  renderer.displayBuffer();
}

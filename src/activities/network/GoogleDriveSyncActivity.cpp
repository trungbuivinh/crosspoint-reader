#include "GoogleDriveSyncActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstdio>

#include "GoogleDriveStore.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "util/BookCacheUtils.h"
#include "util/StringUtils.h"

void GoogleDriveSyncActivity::onEnter() {
  Activity::onEnter();

  state = SyncState::CHECK_WIFI;
  files.clear();
  pending.clear();
  currentIndex = 0;
  downloadedCount = 0;
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
  files.clear();
  pending.clear();

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

void GoogleDriveSyncActivity::startListing() {
  state = SyncState::LISTING;
  requestUpdate(true);

  if (!GoogleDriveClient::listFolder(GDRIVE_STORE.getFolderId(), GDRIVE_STORE.getApiKey(), files)) {
    state = SyncState::ERROR;
    errorMessage = tr(STR_GDRIVE_LIST_FAILED);
    requestUpdate();
    return;
  }

  buildPendingList();
  state = SyncState::READY;
  requestUpdate();
}

std::string GoogleDriveSyncActivity::localPathFor(const DriveFileEntry& file) {
  return "/" + StringUtils::sanitizeFilename(file.name);
}

void GoogleDriveSyncActivity::buildPendingList() {
  pending.clear();
  pending.reserve(files.size());

  for (size_t i = 0; i < files.size(); i++) {
    const std::string path = localPathFor(files[i]);
    bool needsDownload = true;
    if (Storage.exists(path.c_str())) {
      HalFile f;
      if (Storage.openFileForRead("GDRV", path.c_str(), f)) {
        needsDownload = static_cast<uint32_t>(f.size()) != files[i].size;
      }
    }
    if (needsDownload) {
      pending.push_back(static_cast<uint16_t>(i));
    }
  }

  LOG_DBG("GDRV", "%zu of %zu files need download", pending.size(), files.size());
}

void GoogleDriveSyncActivity::syncNext() {
  if (cancelRequested || currentIndex >= pending.size()) {
    state = SyncState::DONE;
    requestUpdate();
    return;
  }

  const auto& file = files[pending[currentIndex]];
  currentName = file.name;
  fileProgress = 0;
  fileTotal = file.size;
  requestUpdate(true);

  const std::string dest = localPathFor(file);
  const std::string url = GoogleDriveClient::downloadUrl(file.id, GDRIVE_STORE.getApiKey());

  const auto result = HttpDownloader::downloadToFile(
      url, dest,
      [this](const size_t downloaded, const size_t total) {
        fileProgress = downloaded;
        if (total > 0) fileTotal = total;
        // Pump input so the user can cancel mid-file; downloadToFile
        // removes the partial file when the cancel flag aborts it.
        mappedInput.update();
        if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
          cancelRequested = true;
        }
        requestUpdate(true);
      },
      &cancelRequested);

  if (result == HttpDownloader::OK) {
    clearBookCache(dest);
    downloadedCount++;
  } else if (result != HttpDownloader::ABORTED) {
    // Skip-and-continue: one bad file must not kill the whole sync
    LOG_ERR("GDRV", "Download failed (%d): %s", result, file.name.c_str());
    failedCount++;
  }
  currentIndex++;
}

void GoogleDriveSyncActivity::loop() {
  if (state == SyncState::WIFI_SELECTION || state == SyncState::CHECK_WIFI || state == SyncState::LISTING) {
    return;
  }

  if (state == SyncState::SYNCING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      cancelRequested = true;
    }
    syncNext();
    return;
  }

  if (state == SyncState::READY) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && !pending.empty()) {
      state = SyncState::SYNCING;
      currentIndex = 0;
      requestUpdate(true);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  if (state == SyncState::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        startListing();
      } else {
        launchWifiSelection();
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  // NOT_CONFIGURED and DONE: any of Back/Confirm exits
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    finish();
  }
}

void GoogleDriveSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.drawCenteredText(UI_12_FONT_ID, 15, tr(STR_GDRIVE), true, EpdFontFamily::BOLD);

  char buf[96];

  switch (state) {
    case SyncState::NOT_CONFIGURED: {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 60, tr(STR_GDRIVE_NOT_CONFIGURED), true,
                                EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, tr(STR_GDRIVE_SETUP_HINT_1));
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 20, tr(STR_GDRIVE_SETUP_HINT_2));
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 50, tr(STR_GDRIVE_SETUP_HINT_3));
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    case SyncState::CHECK_WIFI:
    case SyncState::WIFI_SELECTION:
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_CHECKING_WIFI));
      break;

    case SyncState::LISTING:
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_GDRIVE_LISTING));
      break;

    case SyncState::READY: {
      if (pending.empty()) {
        renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_GDRIVE_UP_TO_DATE));
        const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
        GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      } else {
        snprintf(buf, sizeof(buf), tr(STR_GDRIVE_FILES_FOUND_FORMAT), static_cast<int>(files.size()),
                 static_cast<int>(pending.size()));
        renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, buf);
        const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_START_SYNC), "", "");
        GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      }
      break;
    }

    case SyncState::SYNCING: {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 70, tr(STR_DOWNLOADING));
      const auto title = renderer.truncatedText(UI_10_FONT_ID, currentName.c_str(), pageWidth - 40);
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 40, title.c_str());
      if (fileTotal > 0) {
        GUI.drawProgressBar(renderer, Rect{50, pageHeight / 2 - 10, pageWidth - 100, 20}, fileProgress, fileTotal);
      }
      snprintf(buf, sizeof(buf), tr(STR_GDRIVE_FILE_OF_FORMAT), static_cast<int>(currentIndex + 1),
               static_cast<int>(pending.size()));
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 30, buf);
      GUI.drawProgressBar(renderer, Rect{50, pageHeight / 2 + 50, pageWidth - 100, 20}, currentIndex, pending.size());
      const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    case SyncState::DONE: {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20,
                                cancelRequested ? tr(STR_SYNC_CANCELLED) : tr(STR_SYNC_COMPLETE), true,
                                EpdFontFamily::BOLD);
      snprintf(buf, sizeof(buf), tr(STR_GDRIVE_RESULT_FORMAT), static_cast<int>(downloadedCount),
               static_cast<int>(failedCount));
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, buf);
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    case SyncState::ERROR: {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_ERROR_MSG));
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, errorMessage.c_str());
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }
  }

  renderer.displayBuffer();
}

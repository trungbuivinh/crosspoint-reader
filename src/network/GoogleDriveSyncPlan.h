#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "GoogleDriveClient.h"

enum class LocalDriveNodeType : uint8_t { DIRECTORY, FILE };

struct LocalDriveNode {
  LocalDriveNodeType type;
  std::string relativePath;
  uint32_t size = 0;
  std::string md5Checksum;
};

enum class SyncPlanError { NONE, REMOTE_COLLISION, LOCAL_COLLISION, INVALID_TREE };

struct GoogleDriveSyncPlan {
  struct CaseRename {
    std::string sourceRelativePath;
    std::string targetRelativePath;
    LocalDriveNodeType localType;
  };

  struct TypeConflict {
    std::string relativePath;
    LocalDriveNodeType localType;
  };

  std::vector<std::string> directoriesToCreate;
  std::vector<size_t> epubsToDownload;
  std::vector<CaseRename> caseRenames;
  std::vector<std::string> filesToDelete;
  std::vector<std::string> directoriesToDelete;
  std::vector<TypeConflict> typeConflicts;
  size_t conflictDescendantFileCount = 0;
  size_t conflictDescendantDirectoryCount = 0;
  size_t unchangedEpubCount = 0;

  size_t actionCount() const {
    return caseRenames.size() + directoriesToCreate.size() + epubsToDownload.size() + filesToDelete.size() +
           directoriesToDelete.size();
  }

  size_t destructiveFileCount() const;
  size_t destructiveDirectoryCount() const;
};

SyncPlanError buildGoogleDriveSyncPlan(const DriveTree& remote, const std::vector<LocalDriveNode>& local,
                                       GoogleDriveSyncPlan& out);

#include "GoogleDriveSyncPlan.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace {

std::string pathKey(std::string path) {
  std::transform(path.begin(), path.end(), path.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return path;
}

size_t pathDepth(const std::string& path) { return static_cast<size_t>(std::count(path.begin(), path.end(), '/')) + 1; }

std::vector<std::string> pathComponents(const std::string& path) {
  std::vector<std::string> components;
  size_t start = 0;
  while (start <= path.size()) {
    const size_t end = path.find('/', start);
    components.push_back(path.substr(start, end - start));
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return components;
}

std::string joinComponents(const std::vector<std::string>& components, const size_t lastIndex,
                           const std::string& lastComponent) {
  std::string path;
  for (size_t i = 0; i <= lastIndex; i++) {
    if (!path.empty()) path += '/';
    path += i == lastIndex ? lastComponent : components[i];
  }
  return path;
}

}  // namespace

size_t GoogleDriveSyncPlan::destructiveFileCount() const {
  return filesToDelete.size() + conflictDescendantFileCount +
         static_cast<size_t>(std::count_if(typeConflicts.begin(), typeConflicts.end(), [](const auto& conflict) {
           return conflict.localType == LocalDriveNodeType::FILE;
         }));
}

size_t GoogleDriveSyncPlan::destructiveDirectoryCount() const {
  return directoriesToDelete.size() + conflictDescendantDirectoryCount +
         static_cast<size_t>(std::count_if(typeConflicts.begin(), typeConflicts.end(), [](const auto& conflict) {
           return conflict.localType == LocalDriveNodeType::DIRECTORY;
         }));
}

SyncPlanError buildGoogleDriveSyncPlan(const DriveTree& remote, const std::vector<LocalDriveNode>& local,
                                       GoogleDriveSyncPlan& out) {
  out = {};

  std::unordered_map<std::string, size_t> remoteByPath;
  remoteByPath.reserve(remote.nodes.size());
  for (size_t i = 0; i < remote.nodes.size(); i++) {
    if (!remoteByPath.emplace(pathKey(remote.nodes[i].relativePath), i).second) {
      return SyncPlanError::REMOTE_COLLISION;
    }
  }

  std::unordered_map<std::string, size_t> localByPath;
  localByPath.reserve(local.size());
  for (size_t i = 0; i < local.size(); i++) {
    if (!localByPath.emplace(pathKey(local[i].relativePath), i).second) {
      return SyncPlanError::LOCAL_COLLISION;
    }
  }

  std::unordered_set<std::string> caseRenameTargets;

  for (size_t i = 0; i < remote.nodes.size(); i++) {
    const auto& remoteNode = remote.nodes[i];
    const auto localIt = localByPath.find(pathKey(remoteNode.relativePath));
    if (localIt != localByPath.end()) {
      const auto& localNode = local[localIt->second];
      const bool sameType =
          (localNode.type == LocalDriveNodeType::DIRECTORY && remoteNode.type == DriveNodeType::DIRECTORY) ||
          (localNode.type == LocalDriveNodeType::FILE && remoteNode.type == DriveNodeType::EPUB);
      if (sameType && localNode.relativePath != remoteNode.relativePath) {
        const auto localComponents = pathComponents(localNode.relativePath);
        const auto remoteComponents = pathComponents(remoteNode.relativePath);
        if (localComponents.size() != remoteComponents.size()) return SyncPlanError::INVALID_TREE;
        for (size_t component = 0; component < remoteComponents.size(); component++) {
          if (localComponents[component] == remoteComponents[component]) continue;
          const std::string target = joinComponents(remoteComponents, component, remoteComponents[component]);
          if (!caseRenameTargets.insert(target).second) continue;
          const std::string source = joinComponents(remoteComponents, component, localComponents[component]);
          const LocalDriveNodeType type =
              component + 1 == remoteComponents.size() ? localNode.type : LocalDriveNodeType::DIRECTORY;
          out.caseRenames.push_back({source, target, type});
        }
      }
    }
    if (remoteNode.type == DriveNodeType::DIRECTORY) {
      if (localIt == localByPath.end() || local[localIt->second].type != LocalDriveNodeType::DIRECTORY) {
        out.directoriesToCreate.push_back(remoteNode.relativePath);
      }
      continue;
    }

    bool needsDownload = true;
    if (localIt != localByPath.end()) {
      const auto& localNode = local[localIt->second];
      if (localNode.type == LocalDriveNodeType::FILE && localNode.size == remoteNode.size &&
          !remoteNode.md5Checksum.empty() && localNode.md5Checksum == remoteNode.md5Checksum) {
        needsDownload = false;
        out.unchangedEpubCount++;
      }
    }
    if (needsDownload) {
      out.epubsToDownload.push_back(i);
    }
  }

  for (const auto& localNode : local) {
    const auto remoteIt = remoteByPath.find(pathKey(localNode.relativePath));
    if (remoteIt != remoteByPath.end()) {
      const auto& remoteNode = remote.nodes[remoteIt->second];
      const bool keep =
          (localNode.type == LocalDriveNodeType::DIRECTORY && remoteNode.type == DriveNodeType::DIRECTORY) ||
          (localNode.type == LocalDriveNodeType::FILE && remoteNode.type == DriveNodeType::EPUB);
      if (!keep) {
        out.typeConflicts.push_back({localNode.relativePath, localNode.type});
      }
    }
  }

  for (const auto& localNode : local) {
    if (remoteByPath.contains(pathKey(localNode.relativePath))) {
      continue;
    }
    const bool coveredByDirectoryConflict =
        std::any_of(out.typeConflicts.begin(), out.typeConflicts.end(), [&](const auto& conflict) {
          if (conflict.localType != LocalDriveNodeType::DIRECTORY ||
              localNode.relativePath.size() <= conflict.relativePath.size()) {
            return false;
          }
          return localNode.relativePath.compare(0, conflict.relativePath.size(), conflict.relativePath) == 0 &&
                 localNode.relativePath[conflict.relativePath.size()] == '/';
        });
    if (!coveredByDirectoryConflict) {
      if (localNode.type == LocalDriveNodeType::DIRECTORY) {
        out.directoriesToDelete.push_back(localNode.relativePath);
      } else {
        out.filesToDelete.push_back(localNode.relativePath);
      }
    } else if (localNode.type == LocalDriveNodeType::DIRECTORY) {
      out.conflictDescendantDirectoryCount++;
    } else {
      out.conflictDescendantFileCount++;
    }
  }

  std::sort(out.directoriesToCreate.begin(), out.directoriesToCreate.end(), [](const auto& a, const auto& b) {
    const size_t aDepth = pathDepth(a);
    const size_t bDepth = pathDepth(b);
    return aDepth == bDepth ? a < b : aDepth < bDepth;
  });
  std::sort(out.epubsToDownload.begin(), out.epubsToDownload.end(),
            [&remote](size_t a, size_t b) { return remote.nodes[a].relativePath < remote.nodes[b].relativePath; });
  std::sort(out.caseRenames.begin(), out.caseRenames.end(), [](const auto& a, const auto& b) {
    const size_t aDepth = pathDepth(a.targetRelativePath);
    const size_t bDepth = pathDepth(b.targetRelativePath);
    return aDepth == bDepth ? a.targetRelativePath < b.targetRelativePath : aDepth < bDepth;
  });
  std::sort(out.filesToDelete.begin(), out.filesToDelete.end(), [](const auto& a, const auto& b) {
    const size_t aDepth = pathDepth(a);
    const size_t bDepth = pathDepth(b);
    return aDepth == bDepth ? a < b : aDepth > bDepth;
  });
  std::sort(out.directoriesToDelete.begin(), out.directoriesToDelete.end(), [](const auto& a, const auto& b) {
    const size_t aDepth = pathDepth(a);
    const size_t bDepth = pathDepth(b);
    return aDepth == bDepth ? a < b : aDepth > bDepth;
  });
  std::sort(out.typeConflicts.begin(), out.typeConflicts.end(), [](const auto& a, const auto& b) {
    const size_t aDepth = pathDepth(a.relativePath);
    const size_t bDepth = pathDepth(b.relativePath);
    return aDepth == bDepth ? a.relativePath < b.relativePath : aDepth > bDepth;
  });

  return SyncPlanError::NONE;
}

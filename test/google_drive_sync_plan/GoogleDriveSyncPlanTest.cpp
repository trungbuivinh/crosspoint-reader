#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "network/GoogleDriveSyncPlan.h"

namespace {

DriveNode directory(std::string path) {
  const size_t slash = path.find_last_of('/');
  const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  return {DriveNodeType::DIRECTORY, "dir-" + path, name, std::move(path), 0, ""};
}

DriveNode epub(std::string path, uint32_t size, std::string md5) {
  const size_t slash = path.find_last_of('/');
  const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  return {DriveNodeType::EPUB, "file-" + path, name, std::move(path), size, std::move(md5)};
}

LocalDriveNode localDirectory(std::string path) { return {LocalDriveNodeType::DIRECTORY, std::move(path), 0, ""}; }

LocalDriveNode localFile(std::string path, uint32_t size, std::string md5 = "") {
  return {LocalDriveNodeType::FILE, std::move(path), size, std::move(md5)};
}

TEST(GoogleDriveSyncPlan, CreatesNestedDirectoriesAndDownloadsEpub) {
  DriveTree remote{{directory("Fiction"), directory("Fiction/SciFi"), epub("Fiction/SciFi/Dune.epub", 100, "hash")}};
  GoogleDriveSyncPlan plan;

  ASSERT_EQ(buildGoogleDriveSyncPlan(remote, {}, plan), SyncPlanError::NONE);
  EXPECT_EQ(plan.directoriesToCreate, (std::vector<std::string>{"Fiction", "Fiction/SciFi"}));
  ASSERT_EQ(plan.epubsToDownload.size(), 1u);
  EXPECT_EQ(remote.nodes[plan.epubsToDownload[0]].relativePath, "Fiction/SciFi/Dune.epub");
}

TEST(GoogleDriveSyncPlan, SameSizeDifferentMd5UpdatesButMatchingMd5DoesNot) {
  DriveTree remote{{epub("Changed.epub", 10, "new"), epub("Same.epub", 20, "same")}};
  std::vector<LocalDriveNode> local{localFile("Changed.epub", 10, "old"), localFile("Same.epub", 20, "same")};
  GoogleDriveSyncPlan plan;

  ASSERT_EQ(buildGoogleDriveSyncPlan(remote, local, plan), SyncPlanError::NONE);
  ASSERT_EQ(plan.epubsToDownload.size(), 1u);
  EXPECT_EQ(remote.nodes[plan.epubsToDownload[0]].relativePath, "Changed.epub");
  EXPECT_EQ(plan.unchangedEpubCount, 1u);
}

TEST(GoogleDriveSyncPlan, MissingRemoteMd5AlwaysDownloads) {
  DriveTree remote{{epub("Book.epub", 10, "")}};
  std::vector<LocalDriveNode> local{localFile("Book.epub", 10, "anything")};
  GoogleDriveSyncPlan plan;

  ASSERT_EQ(buildGoogleDriveSyncPlan(remote, local, plan), SyncPlanError::NONE);
  EXPECT_EQ(plan.epubsToDownload.size(), 1u);
}

TEST(GoogleDriveSyncPlan, DeletesAllExtraFilesAndDirectoriesDeepestFirst) {
  DriveTree remote{{directory("Keep")}};
  std::vector<LocalDriveNode> local{localDirectory("Keep"), localDirectory("Old"), localDirectory("Old/Child"),
                                    localFile("Old/Child/book.epub", 1), localFile("notes.txt", 2)};
  GoogleDriveSyncPlan plan;

  ASSERT_EQ(buildGoogleDriveSyncPlan(remote, local, plan), SyncPlanError::NONE);
  EXPECT_EQ(plan.filesToDelete, (std::vector<std::string>{"Old/Child/book.epub", "notes.txt"}));
  EXPECT_EQ(plan.directoriesToDelete, (std::vector<std::string>{"Old/Child", "Old"}));
}

TEST(GoogleDriveSyncPlan, EmptyRemoteDeletesChildrenButNeverRepresentsMirrorRoot) {
  DriveTree remote;
  std::vector<LocalDriveNode> local{localDirectory("Category"), localFile("Category/book.epub", 1)};
  GoogleDriveSyncPlan plan;

  ASSERT_EQ(buildGoogleDriveSyncPlan(remote, local, plan), SyncPlanError::NONE);
  EXPECT_EQ(plan.filesToDelete, (std::vector<std::string>{"Category/book.epub"}));
  EXPECT_EQ(plan.directoriesToDelete, (std::vector<std::string>{"Category"}));
}

TEST(GoogleDriveSyncPlan, TypeConflictIsBackedUpInsteadOfScheduledAsOrdinaryDeletion) {
  DriveTree remote{{epub("Category.epub", 10, "new")}};
  std::vector<LocalDriveNode> local{localDirectory("Category.epub"), localFile("Category.epub/old.txt", 2)};
  GoogleDriveSyncPlan plan;

  ASSERT_EQ(buildGoogleDriveSyncPlan(remote, local, plan), SyncPlanError::NONE);
  ASSERT_EQ(plan.typeConflicts.size(), 1u);
  EXPECT_EQ(plan.typeConflicts[0].relativePath, "Category.epub");
  EXPECT_EQ(plan.destructiveFileCount(), 1u);
  EXPECT_EQ(plan.destructiveDirectoryCount(), 1u);
  EXPECT_TRUE(plan.filesToDelete.empty());
  EXPECT_TRUE(plan.directoriesToDelete.empty());
}

TEST(GoogleDriveSyncPlan, RejectsCaseInsensitiveRemoteCollision) {
  DriveTree remote{{epub("Book.epub", 1, "a"), epub("book.epub", 1, "b")}};
  GoogleDriveSyncPlan plan;
  EXPECT_EQ(buildGoogleDriveSyncPlan(remote, {}, plan), SyncPlanError::REMOTE_COLLISION);
}

TEST(GoogleDriveSyncPlan, KeepsSameNamesInDifferentDirectories) {
  DriveTree remote{{directory("A"), directory("B"), epub("A/Book.epub", 1, "a"), epub("B/Book.epub", 1, "b")}};
  GoogleDriveSyncPlan plan;
  EXPECT_EQ(buildGoogleDriveSyncPlan(remote, {}, plan), SyncPlanError::NONE);
  EXPECT_EQ(plan.epubsToDownload.size(), 2u);
}

TEST(GoogleDriveSyncPlan, RenamesEveryMismatchedPathComponentToDriveCasing) {
  DriveTree remote{{directory("Fiction"), epub("Fiction/Book.epub", 10, "same")}};
  std::vector<LocalDriveNode> local{localDirectory("fiction"), localFile("fiction/book.epub", 10, "same")};
  GoogleDriveSyncPlan plan;

  ASSERT_EQ(buildGoogleDriveSyncPlan(remote, local, plan), SyncPlanError::NONE);
  ASSERT_EQ(plan.caseRenames.size(), 2u);
  EXPECT_EQ(plan.caseRenames[0].sourceRelativePath, "fiction");
  EXPECT_EQ(plan.caseRenames[0].targetRelativePath, "Fiction");
  EXPECT_EQ(plan.caseRenames[0].localType, LocalDriveNodeType::DIRECTORY);
  EXPECT_EQ(plan.caseRenames[1].sourceRelativePath, "Fiction/book.epub");
  EXPECT_EQ(plan.caseRenames[1].targetRelativePath, "Fiction/Book.epub");
  EXPECT_EQ(plan.caseRenames[1].localType, LocalDriveNodeType::FILE);
  EXPECT_TRUE(plan.epubsToDownload.empty());
  EXPECT_EQ(plan.actionCount(), 2u);
}

}  // namespace

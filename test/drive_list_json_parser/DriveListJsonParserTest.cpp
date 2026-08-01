#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "lib/JsonParser/DriveListJsonParser.h"

namespace {

struct CollectedFile {
  std::string id;
  std::string name;
  std::string mimeType;
  std::string md5Checksum;
  uint32_t size;
  bool idTruncated;
  bool nameTruncated;
  bool md5Truncated;
};

struct Collector {
  std::vector<CollectedFile> files;

  static void onFile(void* ctx, const DriveFileInfo& f) {
    auto* self = static_cast<Collector*>(ctx);
    self->files.push_back(
        {f.id, f.name, f.mimeType, f.md5Checksum, f.size, f.idTruncated, f.nameTruncated, f.md5Truncated});
  }
};

// Realistic Drive API v3 files.list response: mixed entries including a
// Google-native doc (no size) and the string-typed "size" the API sends.
const char* kRealisticPage = R"({
 "kind": "drive#fileList",
 "incompleteSearch": false,
 "files": [
  {
   "kind": "drive#file",
   "mimeType": "application/epub+zip",
   "id": "1AbCdEfGhIjKlMnOpQrStUvWxYz012345",
   "name": "Pride and Prejudice.epub",
   "size": "1234567",
   "md5Checksum": "0123456789abcdef0123456789abcdef"
  },
  {
   "kind": "drive#file",
   "mimeType": "application/vnd.google-apps.document",
   "id": "1GoogleDocIdWithoutAnySizeField00",
   "name": "My Notes"
  },
  {
   "kind": "drive#file",
   "mimeType": "text/plain",
   "id": "1TxTiDxTxTiDxTxTiDxTxTiDxTxTiDx00",
   "name": "story.txt",
   "size": "9876"
  }
 ]
})";

TEST(DriveListJsonParser, ParsesRealisticPage) {
  Collector c;
  DriveListJsonParser parser(&c, Collector::onFile);
  parser.feed(kRealisticPage, strlen(kRealisticPage));

  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(c.files.size(), 3u);

  EXPECT_EQ(c.files[0].id, "1AbCdEfGhIjKlMnOpQrStUvWxYz012345");
  EXPECT_EQ(c.files[0].name, "Pride and Prejudice.epub");
  EXPECT_EQ(c.files[0].mimeType, "application/epub+zip");
  EXPECT_EQ(c.files[0].size, 1234567u);
  EXPECT_EQ(c.files[0].md5Checksum, "0123456789abcdef0123456789abcdef");
  EXPECT_FALSE(c.files[0].md5Truncated);

  // Google-native doc: no "size" key at all -> size must default to 0
  EXPECT_EQ(c.files[1].mimeType, "application/vnd.google-apps.document");
  EXPECT_EQ(c.files[1].size, 0u);

  EXPECT_EQ(c.files[2].name, "story.txt");
  EXPECT_EQ(c.files[2].size, 9876u);

  // No nextPageToken in this response
  EXPECT_STREQ(parser.getNextPageToken(), "");
}

TEST(DriveListJsonParser, CapturesNextPageToken) {
  Collector c;
  DriveListJsonParser parser(&c, Collector::onFile);
  const char* json = R"({"nextPageToken":"~!!~AI9FV7RfSmXtJd0123456789abcdefghijklmnop","files":[)"
                     R"({"id":"a1","name":"b.epub","mimeType":"application/epub+zip","size":"1"}]})";
  parser.feed(json, strlen(json));

  ASSERT_FALSE(parser.hasError());
  EXPECT_STREQ(parser.getNextPageToken(), "~!!~AI9FV7RfSmXtJd0123456789abcdefghijklmnop");
  ASSERT_EQ(c.files.size(), 1u);
}

TEST(DriveListJsonParser, TokenAfterFilesArrayIsCaptured) {
  // Key order is not guaranteed; nextPageToken may follow the files array
  Collector c;
  DriveListJsonParser parser(&c, Collector::onFile);
  const char* json = R"({"files":[{"id":"a1","name":"b.epub","mimeType":"application/epub+zip","size":"1"}],)"
                     R"("nextPageToken":"tok123"})";
  parser.feed(json, strlen(json));

  ASSERT_FALSE(parser.hasError());
  EXPECT_STREQ(parser.getNextPageToken(), "tok123");
  ASSERT_EQ(c.files.size(), 1u);
}

TEST(DriveListJsonParser, HandlesChunkedFeeding) {
  // Split the response at awkward boundaries (mid-key, mid-value, mid-number)
  Collector c;
  DriveListJsonParser parser(&c, Collector::onFile);
  const std::string json = kRealisticPage;
  for (size_t chunk = 1; chunk <= 7; chunk += 3) {
    c.files.clear();
    parser.reset();
    for (size_t i = 0; i < json.size(); i += chunk) {
      parser.feed(json.data() + i, std::min(chunk, json.size() - i));
    }
    ASSERT_FALSE(parser.hasError()) << "chunk size " << chunk;
    ASSERT_EQ(c.files.size(), 3u) << "chunk size " << chunk;
    EXPECT_EQ(c.files[0].size, 1234567u) << "chunk size " << chunk;
  }
}

TEST(DriveListJsonParser, EmptyFilesArray) {
  Collector c;
  DriveListJsonParser parser(&c, Collector::onFile);
  const char* json = R"({"kind":"drive#fileList","incompleteSearch":false,"files":[]})";
  parser.feed(json, strlen(json));

  ASSERT_FALSE(parser.hasError());
  EXPECT_TRUE(c.files.empty());
  EXPECT_STREQ(parser.getNextPageToken(), "");
}

TEST(DriveListJsonParser, TruncatesOverlongName) {
  Collector c;
  DriveListJsonParser parser(&c, Collector::onFile);
  std::string longName(300, 'x');
  std::string json =
      R"({"files":[{"id":"a1","name":")" + longName + R"(","mimeType":"application/epub+zip","size":"5"}]})";
  parser.feed(json.c_str(), json.size());

  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(c.files.size(), 1u);
  EXPECT_EQ(c.files[0].name.size(), sizeof(DriveFileInfo::name) - 1);
  EXPECT_EQ(c.files[0].size, 5u);
  EXPECT_TRUE(c.files[0].nameTruncated);
}

TEST(DriveListJsonParser, ParsesFolderAndResetsMd5BetweenEntries) {
  Collector c;
  DriveListJsonParser parser(&c, Collector::onFile);
  const char* json = R"({"files":[{"id":"f1","name":"Fiction","mimeType":"application/vnd.google-apps.folder"},)"
                     R"({"id":"b1","name":"book.epub","mimeType":"application/epub+zip","size":"2",)"
                     R"("md5Checksum":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},)"
                     R"({"id":"b2","name":"other.epub","mimeType":"application/epub+zip","size":"3"}]})";
  parser.feed(json, strlen(json));

  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(c.files.size(), 3u);
  EXPECT_EQ(c.files[0].mimeType, "application/vnd.google-apps.folder");
  EXPECT_TRUE(c.files[0].md5Checksum.empty());
  EXPECT_EQ(c.files[1].md5Checksum, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  EXPECT_TRUE(c.files[2].md5Checksum.empty());
}

TEST(DriveListJsonParser, SkipsFileWithoutId) {
  // An object with no id (should not be emitted; guards against malformed entries)
  Collector c;
  DriveListJsonParser parser(&c, Collector::onFile);
  const char* json = R"({"files":[{"name":"orphan.epub","mimeType":"application/epub+zip"},)"
                     R"({"id":"ok1","name":"good.epub","mimeType":"application/epub+zip","size":"2"}]})";
  parser.feed(json, strlen(json));

  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(c.files.size(), 1u);
  EXPECT_EQ(c.files[0].id, "ok1");
}

TEST(DriveListJsonParser, IgnoresNestedObjectsInsideFileEntry) {
  // Future-proofing: nested objects inside a file entry must not confuse
  // field capture or entry commit
  Collector c;
  DriveListJsonParser parser(&c, Collector::onFile);
  const char* json = R"({"files":[{"id":"n1","name":"nested.epub",)"
                     R"("capabilities":{"canDownload":true,"owners":[{"name":"someone"}]},)"
                     R"("mimeType":"application/epub+zip","size":"77"}]})";
  parser.feed(json, strlen(json));

  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(c.files.size(), 1u);
  EXPECT_EQ(c.files[0].id, "n1");
  EXPECT_EQ(c.files[0].name, "nested.epub");
  EXPECT_EQ(c.files[0].size, 77u);
}

TEST(DriveListJsonParser, ResetClearsStateBetweenPages) {
  Collector c;
  DriveListJsonParser parser(&c, Collector::onFile);
  const char* page1 = R"({"nextPageToken":"tokA","files":[{"id":"p1","name":"a.epub","size":"1"}]})";
  parser.feed(page1, strlen(page1));
  EXPECT_STREQ(parser.getNextPageToken(), "tokA");

  parser.reset();
  EXPECT_STREQ(parser.getNextPageToken(), "");

  const char* page2 = R"({"files":[{"id":"p2","name":"b.epub","size":"2"}]})";
  parser.feed(page2, strlen(page2));
  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(c.files.size(), 2u);
  EXPECT_EQ(c.files[1].id, "p2");
  EXPECT_STREQ(parser.getNextPageToken(), "");
}

}  // namespace

#include <gtest/gtest.h>

#include "OtaVersion.h"

using OtaVersion::Comparison;

TEST(OtaVersion, CustomRevisionUpdateIsNewer) {
  EXPECT_EQ(OtaVersion::compare("1.5.0", "1.5.0.1"), Comparison::Newer);
  EXPECT_EQ(OtaVersion::compare("1.5.0.1", "1.5.0.2"), Comparison::Newer);
}

TEST(OtaVersion, OfficialTagPrefixIsAccepted) {
  EXPECT_EQ(OtaVersion::compare("1.4.1.4", "v1.5.0"), Comparison::Newer);
  EXPECT_EQ(OtaVersion::compare("1.5.0.1", "v1.5.0"), Comparison::Older);
  EXPECT_EQ(OtaVersion::compare("v1.5.0", "v1.5.0"), Comparison::Equal);
}

TEST(OtaVersion, ThreeComponentsNormalizeToZeroCustomRevision) {
  EXPECT_EQ(OtaVersion::compare("1.5.0.1", "1.5.0"), Comparison::Older);
  EXPECT_EQ(OtaVersion::compare("1.5.0.0", "1.5.0"), Comparison::Equal);
  EXPECT_EQ(OtaVersion::compare("1.5.0", "1.5.0.1"), Comparison::Newer);
}

TEST(OtaVersion, EqualAndOlderVersionsAreNotUpdates) {
  EXPECT_EQ(OtaVersion::compare("2.3.4.1", "2.3.4.1"), Comparison::Equal);
  EXPECT_EQ(OtaVersion::compare("2.3.4.1", "2.3.4.0"), Comparison::Older);
  EXPECT_EQ(OtaVersion::compare("2.0.0", "1.99.99.99"), Comparison::Older);
}

TEST(OtaVersion, StableReleaseSupersedesMatchingReleaseCandidate) {
  EXPECT_EQ(OtaVersion::compare("1.5.0-rc+abc123", "v1.5.0"), Comparison::Newer);
  EXPECT_EQ(OtaVersion::compare("1.5.0.1-rc2", "1.5.0.1"), Comparison::Newer);
}

TEST(OtaVersion, CurrentBuildSuffixIsAccepted) {
  EXPECT_EQ(OtaVersion::compare("1.5.0.0-dev-release/1.5.0.0-abc123", "v1.5.0"), Comparison::Equal);
  EXPECT_EQ(OtaVersion::compare("1.5.0.0+local.123", "1.5.0.1"), Comparison::Newer);
}

TEST(OtaVersion, MalformedCandidateIsRejected) {
  constexpr const char* malformedVersions[] = {"",        "1.2",         "1.2.3.4.5", "vv1.2.3", "1.2.x",
                                               "1..2.3",  "1.2.3-rc",    "v",           "1.2.3.4.",
                                               "4294967296.0.0"};
  for (const char* version : malformedVersions) {
    EXPECT_EQ(OtaVersion::compare("1.2.3", version), Comparison::Invalid) << version;
  }
}

TEST(OtaVersion, MalformedCurrentVersionIsRejected) {
  EXPECT_EQ(OtaVersion::compare("1.2", "1.2.3"), Comparison::Invalid);
  EXPECT_EQ(OtaVersion::compare("1.2.3-", "1.2.4"), Comparison::Invalid);
  EXPECT_EQ(OtaVersion::compare(nullptr, "1.2.3"), Comparison::Invalid);
  EXPECT_EQ(OtaVersion::compare("1.2.3", nullptr), Comparison::Invalid);
}

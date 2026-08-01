#include "OtaVersion.h"

#include <cstdint>

namespace {

struct ParsedVersion {
  uint32_t components[4] = {};
  bool releaseCandidate = false;
};

bool isAsciiDigit(const char value) { return value >= '0' && value <= '9'; }

bool isSuffixCharacter(const char value) {
  return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || isAsciiDigit(value) || value == '.' ||
         value == '+' || value == '-' || value == '_' || value == '/';
}

bool parseComponent(const char*& cursor, uint32_t& component) {
  if (!isAsciiDigit(*cursor)) return false;

  uint32_t value = 0;
  do {
    const uint32_t digit = static_cast<uint32_t>(*cursor - '0');
    if (value > (UINT32_MAX - digit) / 10) return false;
    value = value * 10 + digit;
    ++cursor;
  } while (isAsciiDigit(*cursor));

  component = value;
  return true;
}

bool isReleaseCandidateSuffix(const char* suffix) {
  if (suffix[0] != '-' || suffix[1] != 'r' || suffix[2] != 'c') return false;

  const char next = suffix[3];
  return next == '\0' || next == '+' || next == '-' || next == '.' || isAsciiDigit(next);
}

bool parseVersion(const char* version, const bool allowSuffix, ParsedVersion& parsed) {
  if (version == nullptr || *version == '\0') return false;

  const char* cursor = version;
  for (int index = 0; index < 3; ++index) {
    if (!parseComponent(cursor, parsed.components[index])) return false;
    if (index < 2) {
      if (*cursor != '.') return false;
      ++cursor;
    }
  }

  if (*cursor == '.') {
    ++cursor;
    if (!parseComponent(cursor, parsed.components[3])) return false;
  }

  if (*cursor == '\0') return true;
  if (!allowSuffix || (*cursor != '-' && *cursor != '+')) return false;

  const char* suffix = cursor;
  bool hasAlphaNumericCharacter = false;
  for (++cursor; *cursor != '\0'; ++cursor) {
    if (!isSuffixCharacter(*cursor)) return false;
    hasAlphaNumericCharacter = hasAlphaNumericCharacter || (*cursor >= 'a' && *cursor <= 'z') ||
                               (*cursor >= 'A' && *cursor <= 'Z') || isAsciiDigit(*cursor);
  }
  if (!hasAlphaNumericCharacter) return false;

  parsed.releaseCandidate = isReleaseCandidateSuffix(suffix);
  return true;
}

}  // namespace

namespace OtaVersion {

Comparison compare(const char* currentVersion, const char* candidateVersion) {
  ParsedVersion current;
  ParsedVersion candidate;
  if (!parseVersion(currentVersion, true, current) || !parseVersion(candidateVersion, false, candidate)) {
    return Comparison::Invalid;
  }

  for (int index = 0; index < 4; ++index) {
    if (candidate.components[index] > current.components[index]) return Comparison::Newer;
    if (candidate.components[index] < current.components[index]) return Comparison::Older;
  }

  // A stable release supersedes an RC with the same normalized numeric version.
  if (current.releaseCandidate) return Comparison::Newer;

  return Comparison::Equal;
}

}  // namespace OtaVersion

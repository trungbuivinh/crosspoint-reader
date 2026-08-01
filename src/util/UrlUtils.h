#pragma once
#include <string>

namespace UrlUtils {

/**
 * Prepend http:// if no protocol specified (server will redirect to https if needed)
 */
std::string ensureProtocol(const std::string& url);

/**
 * Extract host with protocol from URL (e.g., "http://example.com" from "http://example.com/path")
 */
std::string extractHost(const std::string& url);

/**
 * Build full URL from server URL and path.
 * If path starts with /, it's an absolute path from the host root.
 * Otherwise, it's relative to the server URL.
 */
std::string buildUrl(const std::string& serverUrl, const std::string& path);

/**
 * Percent-encode a string for use as a URL query parameter value
 * (RFC 3986 unreserved characters pass through unchanged).
 */
std::string urlEncode(const std::string& s);

}  // namespace UrlUtils

#pragma once

namespace TrustedRootCertificates {

// Compact root set used by the verified wolfSSL transport. It covers the
// GitHub API/release CDN and Google Drive API endpoints used by this fork.
const char* bundle();

}  // namespace TrustedRootCertificates

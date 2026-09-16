#pragma once

#include <string>

namespace tc002 {

// SHA-256 of a file as lowercase hex, or "" when it cannot be read.
std::string fileSha256(const std::string& path);

// True when the file at path has one of the recorded hashes for that library.
bool vendorFileTrusted(const char* library, const std::string& path);

// dlopen a vendor library and only keep it when the file it resolved to carries a recorded hash.
// Anything else returns nullptr: the port's calls into these libraries assume one exact build.
void* openTrustedVendorLibrary(const char* library, int flags);

// What was checked so far, for /api/v1/tc002/vendor and the log.
std::string vendorStatusJson();

}

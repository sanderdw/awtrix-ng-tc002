#pragma once

#include <string>
#include <vector>

namespace tc002 {

// SHA-256 of a file as lowercase hex, or "" when it cannot be read.
std::string fileSha256(const std::string& path);

// True when the file at path has one of the recorded hashes for that library.
bool vendorFileTrusted(const char* library, const std::string& path);

// Verified: the file carries a recorded hash. Compatible: it does not, but it defines every function
// vendor-fingerprints.json requires of it (only the vendor application lists any). Unknown: neither.
enum class VendorStatus { Unchecked, Verified, Compatible, Unknown };
const char* vendorStatusName(VendorStatus status);

struct VendorCheck {
  VendorStatus status = VendorStatus::Unchecked;
  std::string path, sha256;                    // sha256 is "" when the file cannot be read
  std::vector<std::string> required, missing;  // all required count as missing when the ELF is unreadable
  bool elfReadable = false;
};

// Hashes the file and, for a library with required functions, reads its ELF dynamic symbol table.
// The file is never loaded. Recorded for /api/v1/tc002/vendor like vendorFileTrusted.
VendorCheck checkVendorFile(const char* library, const std::string& path);

// dlopen a vendor library and only keep it when the file it resolved to carries a recorded hash.
// Anything else returns nullptr: the port's calls into these libraries assume one exact build.
void* openTrustedVendorLibrary(const char* library, int flags);

// What was checked so far, for /api/v1/tc002/vendor and the log.
std::string vendorStatusJson();

}

#include "VendorLibrary.h"

#include <dlfcn.h>
#include <link.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>

#include "Sha256.h"
#include "VendorFingerprints.h"

namespace tc002 {

namespace {

struct Checked {
  std::string path, sha256;
  bool trusted = false;
};
std::mutex checkedMutex;
std::map<std::string, Checked> checked;

void record(const char* library, const std::string& path, const std::string& digest, bool trusted) {
  std::lock_guard<std::mutex> lock(checkedMutex);
  checked[library] = Checked{path, digest, trusted};
}

}

std::string fileSha256(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return "";
  Sha256 hash;
  char buffer[16384];
  while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0)
    hash.update(buffer, static_cast<size_t>(in.gcount()));
  return hash.hex();
}

bool vendorFileTrusted(const char* library, const std::string& path) {
  const std::string digest = fileSha256(path);
  bool trusted = false;
  for (const VendorFingerprint* f = kVendorFingerprints; f->library; ++f)
    if (!std::strcmp(f->library, library) && !digest.empty() && digest == f->sha256) trusted = true;
  record(library, path, digest, trusted);
  return trusted;
}

void* openTrustedVendorLibrary(const char* library, int flags) {
  void* handle = dlopen(library, flags);
  if (!handle) {
    record(library, "", "", false);
    std::fprintf(stderr, "TC002 vendor: %s: %s\n", library, dlerror());
    return nullptr;
  }
  link_map* map = nullptr;
  std::string path = dlinfo(handle, RTLD_DI_LINKMAP, &map) == 0 && map && map->l_name ? map->l_name : "";
  if (vendorFileTrusted(library, path)) return handle;
  dlclose(handle);
  std::fprintf(stderr,
               "TC002 vendor: %s at %s is not a build this port was verified against (stock app %s, MCU %s); "
               "the feature that needs it stays off. See docs/VALIDATION.md.\n",
               library, path.c_str(), kStockApp, kStockMcu);
  return nullptr;
}

std::string vendorStatusJson() {
  std::lock_guard<std::mutex> lock(checkedMutex);
  std::string out = std::string("{\"stock\":{\"app\":\"") + kStockApp + "\",\"mcu\":\"" + kStockMcu + "\"},\"libraries\":{";
  bool first = true;
  for (const VendorFingerprint* f = kVendorFingerprints; f->library; ++f) {
    if (out.find("\"" + std::string(f->library) + "\":") != std::string::npos) continue;
    const auto it = checked.find(f->library);
    if (!first) out += ',';
    first = false;
    out += "\"" + std::string(f->library) + "\":{\"expectedPath\":\"" + f->path + "\",\"checked\":" +
           (it != checked.end() ? "true" : "false") + ",\"trusted\":" +
           (it != checked.end() && it->second.trusted ? "true" : "false") + ",\"path\":\"" +
           (it != checked.end() ? it->second.path : "") + "\",\"sha256\":\"" +
           (it != checked.end() ? it->second.sha256 : "") + "\"}";
  }
  for (const auto& kv : checked) {
    if (out.find("\"" + kv.first + "\":") != std::string::npos) continue;
    if (!first) out += ',';
    first = false;
    out += "\"" + kv.first + "\":{\"expectedPath\":\"\",\"checked\":true,\"trusted\":" +
           (kv.second.trusted ? "true" : "false") + ",\"path\":\"" + kv.second.path + "\",\"sha256\":\"" +
           kv.second.sha256 + "\"}";
  }
  return out + "}}";
}

}

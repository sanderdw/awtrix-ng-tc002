#include "VendorLibrary.h"

#include <dlfcn.h>
#include <link.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>

#include "ElfSymbols.h"
#include "Sha256.h"
#include "VendorFingerprints.h"

namespace tc002 {

namespace {

std::mutex checkedMutex;
std::map<std::string, VendorCheck> checked;

void record(const char* library, const VendorCheck& check) {
  std::lock_guard<std::mutex> lock(checkedMutex);
  checked[library] = check;
}

bool hashRecorded(const char* library, const std::string& digest) {
  for (const VendorFingerprint* f = kVendorFingerprints; f->library; ++f)
    if (!std::strcmp(f->library, library) && !digest.empty() && digest == f->sha256) return true;
  return false;
}

std::vector<std::string> requiredFunctions(const std::string& library) {
  std::vector<std::string> names;
  for (const VendorSymbol* s = kVendorSymbols; s->library; ++s)
    if (library == s->library) names.push_back(s->name);
  return names;
}

std::string jsonList(const std::vector<std::string>& names) {
  std::string out = "[";
  for (const std::string& name : names) out += (out.size() > 1 ? ",\"" : "\"") + name + "\"";
  return out + "]";
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
  VendorCheck check;
  check.path = path;
  check.sha256 = fileSha256(path);
  check.status = hashRecorded(library, check.sha256) ? VendorStatus::Verified : VendorStatus::Unknown;
  record(library, check);
  return check.status == VendorStatus::Verified;
}

const char* vendorStatusName(VendorStatus status) {
  switch (status) {
    case VendorStatus::Verified: return "verified";
    case VendorStatus::Compatible: return "compatible";
    case VendorStatus::Unknown: return "unknown";
    default: return "unchecked";
  }
}

VendorCheck checkVendorFile(const char* library, const std::string& path) {
  VendorCheck check;
  check.path = path;
  check.sha256 = fileSha256(path);
  check.required = requiredFunctions(library);
  // Read the symbols even for a recorded build, so every verified clock also exercises this reader.
  if (!check.required.empty()) {
    check.elfReadable = elfMissingFunctions(path, check.required, check.missing);
    if (!check.elfReadable) check.missing = check.required;
  }
  if (hashRecorded(library, check.sha256)) check.status = VendorStatus::Verified;
  else if (!check.required.empty() && check.missing.empty()) check.status = VendorStatus::Compatible;
  else check.status = VendorStatus::Unknown;
  record(library, check);
  return check;
}

void* openTrustedVendorLibrary(const char* library, int flags) {
  void* handle = dlopen(library, flags);
  if (!handle) {
    VendorCheck unavailable;
    unavailable.status = VendorStatus::Unknown;
    record(library, unavailable);
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
  const auto add = [&](const std::string& library, const char* expectedPath, const VendorCheck* c) {
    const std::vector<std::string> required = requiredFunctions(library);
    if (!first) out += ',';
    first = false;
    out += "\"" + library + "\":{\"expectedPath\":\"" + expectedPath + "\",\"checked\":" +
           (c ? "true" : "false") + ",\"trusted\":" +
           (c && c->status == VendorStatus::Verified ? "true" : "false") + ",\"status\":\"" +
           vendorStatusName(c ? c->status : VendorStatus::Unchecked) + "\",\"path\":\"" + (c ? c->path : "") +
           "\",\"sha256\":\"" + (c ? c->sha256 : "") + "\"";
    if (!required.empty())
      out += ",\"requiredSymbols\":" + jsonList(required) + ",\"missingSymbols\":" +
             jsonList(c ? c->missing : std::vector<std::string>{});
    out += "}";
  };
  for (const VendorFile* f = kVendorFiles; f->library; ++f) {
    const auto it = checked.find(f->library);
    add(f->library, f->path, it != checked.end() ? &it->second : nullptr);
  }
  for (const auto& kv : checked) {
    bool listed = false;
    for (const VendorFile* f = kVendorFiles; f->library; ++f) listed = listed || kv.first == f->library;
    if (!listed) add(kv.first, "", &kv.second);
  }
  return out + "}}";
}

}

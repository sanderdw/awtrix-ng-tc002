// The vendor application gate: which fixture libraries pass, that the exact-hash libraries never pass
// by symbols, and that truncated or corrupted files are rejected without reading out of bounds.
//   test-elf-symbols FULL WEAK MISSING UNDEFINED DATA   run the checks
//   test-elf-symbols --query NAME,NAME,... FILE...       print what the reader makes of each FILE (pytest
//                                                        compares it with tools/install.py)
#include "ElfSymbols.h"
#include "VendorLibrary.h"

#include <elf.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "failed at line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static const std::vector<std::string> kLauncher = {"onEasyUIInit", "onEasyUIDeinit", "onStartupApp",
                                                   "_ZN4base13wifiOnAndWaitEi"};

static std::string query(const std::string& path, const std::vector<std::string>& wanted) {
  std::vector<std::string> missing;
  if (!tc002::elfMissingFunctions(path, wanted, missing)) return "unreadable";
  std::string out = "missing:";
  for (size_t i = 0; i < missing.size(); ++i) out += (i ? "," : "") + missing[i];
  return out;
}

static std::vector<char> readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static bool writeFile(const std::string& path, const std::vector<char>& bytes, size_t length) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(length));
  return static_cast<bool>(out);
}

// Where the section-header table ends; the linker puts it last, so any shorter file loses it.
static size_t sectionTableEnd(const std::vector<char>& elf) {
  if (elf.size() > EI_CLASS && elf[EI_CLASS] == ELFCLASS64 && elf.size() >= sizeof(Elf64_Ehdr)) {
    Elf64_Ehdr h; std::memcpy(&h, elf.data(), sizeof(h));
    return size_t(h.e_shoff) + size_t(h.e_shnum) * h.e_shentsize;
  }
  if (elf.size() >= sizeof(Elf32_Ehdr)) {
    Elf32_Ehdr h; std::memcpy(&h, elf.data(), sizeof(h));
    return size_t(h.e_shoff) + size_t(h.e_shnum) * h.e_shentsize;
  }
  return 0;
}

int main(int argc, char** argv) {
  if (argc >= 3 && !std::strcmp(argv[1], "--query")) {
    std::vector<std::string> wanted;
    for (const char* name = argv[2]; *name;) {
      const char* end = std::strchr(name, ',');
      wanted.emplace_back(name, end ? end : name + std::strlen(name));
      name = end ? end + 1 : name + std::strlen(name);
    }
    for (int i = 3; i < argc; ++i) std::puts(query(argv[i], wanted).c_str());
    return 0;
  }
  CHECK(argc == 6);
  const std::string full = argv[1], weak = argv[2], missing = argv[3], undefined = argv[4], data = argv[5];

  CHECK(query(full, kLauncher) == "missing:");
  CHECK(query(weak, kLauncher) == "missing:");
  CHECK(query(missing, kLauncher) == "missing:onEasyUIDeinit");
  CHECK(query(undefined, kLauncher) == "missing:onEasyUIDeinit");
  CHECK(query(data, kLauncher) == "missing:onEasyUIDeinit");
  CHECK(query(full, {"onStartupApp", "notThere", "onEasyUIInit", "alsoNotThere"}) == "missing:notThere,alsoNotThere");
  CHECK(query("/nonexistent/libzkgui.so", kLauncher) == "unreadable");
  CHECK(query("/dev/null", kLauncher) == "unreadable");

  // Only the vendor application can pass by its entry points; the others need a recorded hash.
  tc002::VendorCheck app = tc002::checkVendorFile("libulanzi-bootstrap.so", missing);
  CHECK(app.status == tc002::VendorStatus::Unknown && app.elfReadable);
  CHECK(app.missing == std::vector<std::string>{"onEasyUIDeinit"});
  app = tc002::checkVendorFile("libulanzi-bootstrap.so", "/nonexistent/libzkgui.so");
  CHECK(app.status == tc002::VendorStatus::Unknown && app.sha256.empty() && !app.elfReadable);
  CHECK(app.missing == kLauncher);
  CHECK(tc002::checkVendorFile("libmi_ao.so", full).status == tc002::VendorStatus::Unknown);
  CHECK(tc002::checkVendorFile("libzknet.so", full).status == tc002::VendorStatus::Unknown);
  CHECK(!tc002::vendorFileTrusted("libzknet.so", full));
  app = tc002::checkVendorFile("libulanzi-bootstrap.so", full);
  CHECK(app.status == tc002::VendorStatus::Compatible && app.missing.empty() && app.sha256.size() == 64);
  CHECK(app.required == kLauncher);
  const std::string status = tc002::vendorStatusJson();
  CHECK(status.find("\"libulanzi-bootstrap.so\":{\"expectedPath\":\"/res/lib/libulanzi-bootstrap.so\",\"checked\":true,"
                    "\"trusted\":false,\"status\":\"compatible\"") != std::string::npos);
  CHECK(status.find("\"requiredSymbols\":[\"onEasyUIInit\",\"onEasyUIDeinit\",\"onStartupApp\","
                    "\"_ZN4base13wifiOnAndWaitEi\"],\"missingSymbols\":[]") != std::string::npos);
  CHECK(status.find("\"libzknet.so\":{\"expectedPath\":\"/lib/libzknet.so\",\"checked\":true,\"trusted\":false,"
                    "\"status\":\"unknown\"") != std::string::npos);

  // Truncations and corruptions: never crash or read past the file (the sanitizers watch this when
  // available), and a file cut before its section headers is unreadable.
  const std::vector<char> elf = readFile(full);
  CHECK(elf.size() > sizeof(Elf64_Ehdr) && sectionTableEnd(elf) == elf.size());
  char scratch[] = "/tmp/tc002-elf-XXXXXX";
  const int fd = mkstemp(scratch);
  CHECK(fd >= 0);
  close(fd);
  const std::string path = scratch;
  for (size_t length = 0; length < elf.size(); length += length < 128 ? 1 : 97) {
    CHECK(writeFile(path, elf, length));
    CHECK(query(path, kLauncher) == "unreadable");
  }
  std::mt19937 random(20260925);
  for (int round = 0; round < 3000; ++round) {
    std::vector<char> copy = elf;
    const int flips = 1 + int(random() % 4);
    for (int i = 0; i < flips; ++i) {
      // Half the flips land in the ELF header or the section-header table, where they matter most.
      const size_t tableStart = sectionTableEnd(elf) - std::min<size_t>(elf.size() / 4, 2048);
      size_t at = random() % copy.size();
      if (round % 2) at = random() % 2 ? random() % 64 : tableStart + random() % (copy.size() - tableStart);
      copy[at] = char(random());
    }
    CHECK(writeFile(path, copy, copy.size()));
    const std::string result = query(path, kLauncher);
    CHECK(result == "unreadable" || result.compare(0, 8, "missing:") == 0);
  }
  unlink(path.c_str());
  std::puts("vendor application gate: fixtures, hash-only libraries, truncation and corruption");
  return 0;
}

#include "Tc002Dns.h"
#include "persistence/DeviceConfig.h"
#include <arpa/inet.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/mount.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

namespace tc002 {
std::string resolverConfig(const std::vector<std::string>& servers) {
  std::string out;
  std::vector<std::string> accepted;
  for (const auto& server : servers) {
    in_addr address{};
    if (inet_pton(AF_INET, server.c_str(), &address) != 1 || address.s_addr == 0 ||
        std::find(accepted.begin(), accepted.end(), server) != accepted.end()) continue;
    accepted.push_back(server);
    out += "nameserver " + server + "\n";
    if (accepted.size() == 3) break; // libc's MAXNS
  }
  return out;
}

namespace {
std::string property(const char* name) {
  static void* library = dlopen("libcutils.so", RTLD_LAZY | RTLD_LOCAL);
  using Get = int (*)(const char*, char*, const char*);
  static Get get = library ? reinterpret_cast<Get>(dlsym(library, "property_get")) : nullptr;
  char value[128]{}; // vendor PROPERTY_VALUE_MAX is 92
  return get && get(name, value, "") > 0 ? value : "";
}
std::string configuredDns(const awtrix::DeviceConfig& config) {
  if (config.netStatic) return resolverConfig({config.dns1, config.dns2});
  auto out = resolverConfig({property("net.wlan0.dns1"), property("net.wlan0.dns2")});
  if (out.empty()) out = resolverConfig({property("net.dns1"), property("net.dns2")});
  return out;
}
}

Dns::~Dns() { stop(); }
void Dns::begin(const awtrix::DeviceConfig& config) {
  // Must run before board initialization or any application worker threads.
  if (unshare(CLONE_NEWNS) < 0 || mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) < 0) {
    std::perror("TC002 DNS: private mount namespace");
    return;
  }
  char path[] = "/tmp/awtrix-resolv-XXXXXX";
  int fd = mkstemp(path);
  if (fd < 0) { std::perror("TC002 DNS: temporary resolver file"); return; }
  close(fd);
  path_ = path;
  refresh(config);
}
void Dns::refresh(const awtrix::DeviceConfig& config) {
  if (path_.empty()) return;
  const auto content = configuredDns(config);
  // Keep the existing resolver if DHCP has not supplied a usable server yet.
  if (content.empty() || (mounted_ && content == current_)) return;
  int fd = open(path_.c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC);
  if (fd < 0) { std::perror("TC002 DNS: open resolver file"); return; }
  const bool written = write(fd, content.data(), content.size()) == static_cast<ssize_t>(content.size());
  close(fd);
  if (!written) { std::perror("TC002 DNS: write resolver file"); return; }
  if (!mounted_) {
    if (mount(path_.c_str(), "/etc/resolv.conf", nullptr, MS_BIND, nullptr) < 0) {
      std::perror("TC002 DNS: mount resolver file");
      return;
    }
    mounted_ = true;
  }
  current_ = content;
  std::fprintf(stderr, "TC002 DNS: using %s DNS servers\n", config.netStatic ? "static" : "DHCP");
}
void Dns::stop() {
  if (mounted_) {
    if (umount2("/etc/resolv.conf", MNT_DETACH) < 0) {
      std::perror("TC002 DNS: unmount resolver file");
      return;
    }
    mounted_ = false;
  }
  if (!path_.empty()) unlink(path_.c_str());
  path_.clear();
  current_.clear();
}
}

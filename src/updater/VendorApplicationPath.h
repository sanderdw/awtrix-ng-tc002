#pragma once

#include <cerrno>
#include <string>
#include <sys/stat.h>

namespace tc002 {

// The stock image names its GUI libzkgui.so. Installation moves that same file
// aside before putting the AWTRIX launcher at its original path.
inline std::string vendorApplicationPath(const std::string& resRoot = "/res") {
  const std::string saved = resRoot + "/lib/libulanzi-bootstrap.so";
  struct stat info{};
  if (lstat(saved.c_str(), &info) == 0 || errno != ENOENT) return saved;
  return resRoot + "/lib/libzkgui.so";
}

}

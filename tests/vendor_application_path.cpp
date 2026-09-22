#include "VendorApplicationPath.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

int main() {
  char pattern[] = "/tmp/tc002-vendor-XXXXXX";
  char* root = mkdtemp(pattern);
  if (!root) return 1;
  const std::string lib = std::string(root) + "/lib";
  const std::string stock = lib + "/libzkgui.so";
  const std::string saved = lib + "/libulanzi-bootstrap.so";
  if (mkdir(lib.c_str(), 0700) != 0) return 1;
  if (FILE* file = std::fopen(stock.c_str(), "wb")) std::fclose(file);
  else return 1;
  const bool stockSelected = tc002::vendorApplicationPath(root) == stock;
  if (FILE* file = std::fopen(saved.c_str(), "wb")) std::fclose(file);
  else return 1;
  const bool installedSelected = tc002::vendorApplicationPath(root) == saved;
  unlink(saved.c_str());
  if (symlink("missing-vendor-app", saved.c_str()) != 0) return 1;
  // A broken preserved copy must be checked and rejected, not bypassed via libzkgui.so.
  const bool brokenSavedSelected = tc002::vendorApplicationPath(root) == saved;
  unlink(saved.c_str());
  unlink(stock.c_str());
  rmdir(lib.c_str());
  rmdir(root);
  if (!stockSelected || !installedSelected || !brokenSavedSelected) {
    std::fprintf(stderr, "Updater selected the wrong vendor application path\n");
    return 1;
  }
  return 0;
}

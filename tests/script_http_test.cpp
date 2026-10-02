#include "Tc002ScriptHttp.h"
#include <cstdio>
#include <cstdlib>

using awtrix::Tc002ScriptHttp;

void check(bool ok, const char* what) {
  if (!ok) { std::fprintf(stderr, "script http headers: %s\n", what); std::exit(1); }
}
std::size_t contentTypes(const httplib::Headers& headers) { return headers.count("Content-Type"); }

int main() {
  // Issue #11: the script's own Content-Type, in any case, is sent once and unchanged.
  auto h = Tc002ScriptHttp::requestHeaders(
      {{"content-type", "application/x-www-form-urlencoded"}, {"Authorization", "Basic x"}}, "a=b");
  check(contentTypes(h) == 1, "script Content-Type duplicated");
  check(h.find("Content-Type")->second == "application/x-www-form-urlencoded", "script Content-Type replaced");
  check(h.count("Authorization") == 1, "other header dropped");

  h = Tc002ScriptHttp::requestHeaders({}, "raw");
  check(contentTypes(h) == 1 && h.find("Content-Type")->second == "application/octet-stream",
        "body without a type gets no default");

  h = Tc002ScriptHttp::requestHeaders({{"Accept", "application/json"}}, "");
  check(contentTypes(h) == 0, "request without a body got a Content-Type");
}

#include "Tc002Dns.h"
#include <cstdio>
#include <cstdlib>

void check(const std::string& actual, const std::string& expected) {
  if (actual != expected) { std::fprintf(stderr, "unexpected resolver config: %s", actual.c_str()); std::exit(1); }
}
int main() {
  check(tc002::resolverConfig({"192.168.100.5", "192.168.100.1"}),
        "nameserver 192.168.100.5\nnameserver 192.168.100.1\n");
  check(tc002::resolverConfig({"", "0.0.0.0", "bad", "192.168.100.5\nnameserver 8.8.8.8"}), "");
  check(tc002::resolverConfig({"192.168.100.5", "192.168.100.5", "192.168.100.1"}),
        "nameserver 192.168.100.5\nnameserver 192.168.100.1\n");
  check(tc002::resolverConfig({"1.1.1.1", "2.2.2.2", "3.3.3.3", "4.4.4.4"}),
        "nameserver 1.1.1.1\nnameserver 2.2.2.2\nnameserver 3.3.3.3\n");
}

#pragma once
#include <string>
namespace tc002 {
class Mdns {
 public:
  ~Mdns();
  void begin(const std::string& hostname,unsigned port);
  void tick();
 private:
  int fd_=-1;
  unsigned port_=80;
  std::string name_;
};
}

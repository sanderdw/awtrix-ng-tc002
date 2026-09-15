#include "sim/vendor/httplib.h"
#include <cstdio>
int main(int argc,char**argv) {
  if(argc!=3) return 2;
  httplib::Client client(argv[1]); client.set_ca_cert_path(argv[2]);
  client.set_connection_timeout(5); client.set_read_timeout(5);
  size_t bytes=0;
  auto result=client.Get("/",[&](const char*,size_t n){bytes+=n;return bytes<65536;});
  if(!result) {
    std::fprintf(stderr,"HTTPS error %d, verify %ld\n",int(result.error()),client.get_openssl_verify_result());
    ERR_print_errors_fp(stderr); return 1;
  }
  std::printf("HTTPS status %d, %zu bytes\n",result->status,bytes);
  return result->status==200 ? 0 : 1;
}

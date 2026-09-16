#include "FirmwareImage.h"
#include <array>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
// ZKSWE requires MD5 for corruption detection. The low-level implementation
// avoids pulling the entire TLS provider into the small recovery helper.
#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/md5.h>
namespace tc002 {
namespace {
uint32_t le32(const unsigned char* p) { return p[0]|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24); }
uint32_t crc(const unsigned char* p,size_t n) {
  uint32_t v=~0u;
  while(n--) { v^=*p++; for(int i=0;i<8;++i) v=(v>>1)^((0u-(v&1))&0xedb88320u); }
  return ~v;
}
}
bool validateFirmware(int fd,FirmwareImage& image,std::string& error) {
  auto fail=[&](const char* s) { error=s; return false; };
  std::array<unsigned char,668> h{};
  struct stat st{};
  if(fstat(fd,&st)!=0 || !S_ISREG(st.st_mode) || pread(fd,h.data(),h.size(),0)!=ssize_t(h.size()))
    return fail("truncated firmware image");
  if(std::memcmp(h.data(),"ZKSWEV1.0-180127",16) || h[16]!=48 || h[17]!=1 || h[18]!=48 || h[20]!=3 ||
     le32(h.data()+48)!=524 || le32(h.data()+53)!=0xaa550606)
    return fail("only a TC002 res-partition update is accepted");
  if(crc(h.data(),568)!=le32(h.data()+568)) return fail("firmware header CRC mismatch");
  image.size=le32(h.data()+28);
  if(le32(h.data()+24)!=572 || image.size<4096 || image.size>0x800000 || image.size%4096 ||
     st.st_size!=572LL+image.size) return fail("invalid firmware bounds");
  std::memcpy(image.first,h.data()+32,16);
  uint64_t used=le32(h.data()+612)|(uint64_t(le32(h.data()+616))<<32);
  if(std::memcmp(image.first,"hsqs",4) || used<96 || used>image.size)
    return fail("invalid squashfs filesystem");
  MD5_CTX ctx;
  bool ok=MD5_Init(&ctx)==1 && MD5_Update(&ctx,image.first,16)==1;
  std::array<unsigned char,65536> data{};
  for(uint32_t pos=16;ok && pos<image.size;) {
    size_t n=std::min<size_t>(data.size(),image.size-pos);
    ok=pread(fd,data.data(),n,572+pos)==ssize_t(n) && MD5_Update(&ctx,data.data(),n)==1;
    pos+=n;
  }
  unsigned char hash[MD5_DIGEST_LENGTH];
  ok=ok && MD5_Final(hash,&ctx)==1 && !std::memcmp(hash,h.data()+572,16);
  return ok || fail("firmware payload MD5 mismatch");
}
}

#include "Tc002Mdns.h"
#include "Tc002Hardware.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <array>
#include <cstring>
#include <cctype>
namespace tc002 {
namespace {
using Bytes=std::vector<unsigned char>;
void word(Bytes& out,unsigned n) { out.push_back(n>>8); out.push_back(n); }
void name(Bytes& out,const std::string& s) {
  for(size_t start=0;start<s.size();) {
    size_t end=s.find('.',start); if(end==std::string::npos) end=s.size();
    out.push_back(end-start); out.insert(out.end(),s.begin()+start,s.begin()+end); start=end+1;
  }
  out.push_back(0);
}
bool readName(const unsigned char* p,size_t n,size_t& pos,std::string& out) {
  size_t at=pos; bool jumped=false; out.clear();
  for(int steps=0;steps<128 && at<n;++steps) {
    unsigned len=p[at++];
    if(!len) { if(!jumped) pos=at; return true; }
    if((len&0xc0)==0xc0) {
      if(at>=n) return false;
      unsigned target=((len&63)<<8)|p[at++];
      if(!jumped) pos=at;
      jumped=true; at=target; continue;
    }
    if(len>63 || at+len>n || out.size()+len>253) return false;
    if(!out.empty()) out+='.';
    while(len--) out+=static_cast<char>(std::tolower(p[at++]));
  }
  return false;
}
void record(Bytes& out,const std::string& key,unsigned type,const Bytes& data,bool unique=true) {
  name(out,key); word(out,type); word(out,unique?0x8001:1);
  word(out,0); word(out,120); word(out,data.size()); out.insert(out.end(),data.begin(),data.end());
}
}
Mdns::~Mdns() { if(fd_>=0) close(fd_); }
void Mdns::begin(const std::string& hostname,unsigned port) {
  name_=hostname; port_=port;
  if(name_.empty() || name_.size()>63) return;
  fd_=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0); if(fd_<0) return;
  int one=1; setsockopt(fd_,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
  sockaddr_in local{}; local.sin_family=AF_INET; local.sin_port=htons(5353);
  ip_mreq membership{}; membership.imr_multiaddr.s_addr=inet_addr("224.0.0.251");
  if(bind(fd_,reinterpret_cast<sockaddr*>(&local),sizeof(local))<0 ||
     setsockopt(fd_,IPPROTO_IP,IP_ADD_MEMBERSHIP,&membership,sizeof(membership))<0) {
    close(fd_); fd_=-1; return;
  }
  unsigned char ttl=255; setsockopt(fd_,IPPROTO_IP,IP_MULTICAST_TTL,&ttl,sizeof(ttl));
}
void Mdns::tick() {
  if(fd_<0) return;
  const auto ip=ipAddress(); if(ip=="127.0.0.1") return;
  const std::string host=name_+".local",http="_http._tcp.local",awtrix="_awtrixng._tcp.local";
  std::array<unsigned char,1500> input{};
  for(int packets=0;packets<16;++packets) {
    sockaddr_in from{}; socklen_t size=sizeof(from);
    ssize_t n=recvfrom(fd_,input.data(),input.size(),0,reinterpret_cast<sockaddr*>(&from),&size);
    if(n<0) return;
    if(n<12 || (input[2]&0x80)) continue;
    size_t pos=12; unsigned questions=(input[4]<<8)|input[5];
    bool matched=false,unicast=from.sin_port!=htons(5353);
    for(unsigned i=0;i<questions && i<32;++i) {
      std::string q;
      if(!readName(input.data(),n,pos,q) || pos+4>size_t(n)) break;
      if(q==host || q==http || q==awtrix || q==name_+"."+http || q==name_+"."+awtrix || q=="_services._dns-sd._udp.local") {
        matched=true; unicast=unicast || (input[pos+2]&0x80);
      }
      pos+=4;
    }
    if(!matched) continue;
    Bytes out(12,0); out[2]=0x84; out[7]=9;
    if(unicast) { out[0]=input[0]; out[1]=input[1]; }
    Bytes addr(4); inet_pton(AF_INET,ip.c_str(),addr.data()); record(out,host,1,addr);
    for(const auto& service:{http,awtrix}) {
      Bytes target; name(target,name_+"."+service); record(out,service,12,target,false);
      Bytes srv(4,0); word(srv,port_); name(srv,host); record(out,name_+"."+service,33,srv);
      Bytes txt;
      for(const auto& field:{"id="+uid(),"name="+name_,std::string("type=awtrixng")}) {
        txt.push_back(field.size()); txt.insert(txt.end(),field.begin(),field.end());
      }
      record(out,name_+"."+service,16,txt);
      Bytes ptr; name(ptr,service); record(out,"_services._dns-sd._udp.local",12,ptr,false);
    }
    if(!unicast) { from.sin_addr.s_addr=inet_addr("224.0.0.251"); from.sin_port=htons(5353); }
    sendto(fd_,out.data(),out.size(),0,reinterpret_cast<sockaddr*>(&from),sizeof(from));
  }
}
}

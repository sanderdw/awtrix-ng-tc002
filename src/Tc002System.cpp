#include "Tc002System.h"
#include "Tc002Hardware.h"
#include "Tc002Mdns.h"
#include "persistence/DeviceConfig.h"
#include "core/net/HostName.h"
#include "system/Log.h"
#include "sim/SimStore.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <dlfcn.h>
#include <spawn.h>
#include <sys/wait.h>
#include <signal.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
extern char** environ;

namespace tc002 {
namespace {
std::mutex stateMutex;
std::string ssidState,scanState="[]";
bool connectedState=false,scanReady=false;
std::atomic<bool> apMode{false};
std::atomic<bool> scanRequested{false};
std::string quote(const std::string& s) {
  std::string out="\"";
  for(unsigned char c:s) {
    if(c=='"' || c=='\\') { out+='\\'; out+=c; }
    else if(c<32) { char b[7]; std::snprintf(b,sizeof(b),"\\u%04x",c); out+=b; }
    else out+=c;
  }
  return out+'"';
}
std::string hex(const std::string& s) {
  const char* h="0123456789abcdef"; std::string out;
  for(unsigned char c:s) { out+=h[c>>4]; out+=h[c&15]; } return out;
}
class Wpa {
 public:
  Wpa() {
    fd_=socket(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC,0); if(fd_<0) return;
    sockaddr_un local{}; local.sun_family=AF_UNIX;
    std::snprintf(local.sun_path,sizeof(local.sun_path),"/tmp/awtrix-wpa-%d",getpid());
    path_=local.sun_path; unlink(path_.c_str());
    sockaddr_un remote{}; remote.sun_family=AF_UNIX;
    std::strcpy(remote.sun_path,"/dev/socket/wlan0");
    if(bind(fd_,reinterpret_cast<sockaddr*>(&local),sizeof(local))<0 ||
       connect(fd_,reinterpret_cast<sockaddr*>(&remote),sizeof(remote))<0) { close(fd_); fd_=-1; }
  }
  ~Wpa() { if(fd_>=0) close(fd_); if(!path_.empty()) unlink(path_.c_str()); }
  std::string command(const std::string& command) {
    if(fd_<0 || send(fd_,command.data(),command.size(),0)!=static_cast<ssize_t>(command.size())) return {};
    pollfd p{fd_,POLLIN,0};
    if(poll(&p,1,700)<=0) return {};
    std::array<char,16384> bytes{};
    auto n=recv(fd_,bytes.data(),bytes.size(),0);
    return n>0 ? std::string(bytes.data(),n) : std::string();
  }
 private:
  int fd_=-1; std::string path_;
};
std::map<std::string,std::string> fields(const std::string& text) {
  std::map<std::string,std::string> result;
  std::istringstream stream(text); std::string line;
  while(std::getline(stream,line)) {
    auto p=line.find('='); if(p!=std::string::npos) result[line.substr(0,p)]=line.substr(p+1);
  }
  return result;
}
std::string scanJson(const std::string& text) {
  std::istringstream stream(text); std::string line,out="[";
  std::getline(stream,line); bool first=true;
  while(std::getline(stream,line)) {
    std::istringstream row(line); std::string bssid,freq,rssi,flags,ssid;
    if(!std::getline(row,bssid,'\t') || !std::getline(row,freq,'\t') ||
       !std::getline(row,rssi,'\t') || !std::getline(row,flags,'\t') || !std::getline(row,ssid)) continue;
    if(ssid.empty()) continue;
    if(!first) out+=',';
    first=false;
    out+="{\"ssid\":"+quote(ssid)+",\"rssi\":"+std::to_string(std::atoi(rssi.c_str()))+
      ",\"enc\":"+(flags.find("WPA")!=std::string::npos || flags.find("WEP")!=std::string::npos ? "true" : "false")+"}";
  }
  return out+"]";
}
bool setNetwork(Wpa& wpa,const std::string& ssid,const std::string& password) {
  std::string id=wpa.command("ADD_NETWORK");
  while(!id.empty() && (id.back()=='\n' || id.back()=='\r')) id.pop_back();
  if(id.empty() || id.find_first_not_of("0123456789")!=std::string::npos) return false;
  auto ok=[&](const std::string& command) { return wpa.command(command).rfind("OK",0)==0; };
  bool success=ok("SET_NETWORK "+id+" ssid "+hex(ssid));
  if(password.empty()) success=success && ok("SET_NETWORK "+id+" key_mgmt NONE");
  else success=success && ok("SET_NETWORK "+id+" psk "+quote(password));
  if(!success) { wpa.command("REMOVE_NETWORK "+id); return false; }
  // Keep previously working networks enabled as a fallback for a mistyped
  // password. Prefer the newly requested network without deleting old entries.
  success=ok("SET_NETWORK "+id+" priority 10") && ok("ENABLE_NETWORK "+id) && ok("REASSOCIATE");
  if(success) wpa.command("SAVE_CONFIG");
  else wpa.command("REMOVE_NETWORK "+id);
  return success;
}
// The standalone DHCP helper is provided by the installed network library.
// Load the same framework dependencies as the stock launcher, without opening
// its GUI or starting its application threads.
void* networkLibrary() {
  static void* net=nullptr;
  if(!net) {
    for(auto name:{"libz.so.1","libpng12.so.0","libjpeg.so.9","libfreetype.so.6",
                  "liblog.so","libcutils.so","libnanovg.so","libmi_sys.so","libmi_gfx.so","libeasyui.so"})
      if(!dlopen(name,RTLD_LAZY|RTLD_GLOBAL)) return nullptr;
    net=dlopen("libzknet.so",RTLD_LAZY|RTLD_GLOBAL);
  }
  return net;
}
bool dhcp() {
  void* net=networkLibrary();
  auto request=net ? reinterpret_cast<bool(*)(const char*)>(dlsym(net,"_ZN8NetUtils13dhcpRequestIpEPKc")) : nullptr;
  return request && request("wlan0");
}
bool staticNetwork(const awtrix::DeviceConfig& cfg) {
  void* net=networkLibrary();
  using Configure=bool(*)(const char*,const char*,const char*,const char*,const char*,const char*);
  auto configure=net ? reinterpret_cast<Configure>(dlsym(net,"_ZN8NetUtils9configureEPKcS1_S1_S1_S1_S1_")) : nullptr;
  return configure && configure("wlan0",cfg.ip.c_str(),cfg.subnet.c_str(),cfg.gateway.c_str(),cfg.dns1.c_str(),cfg.dns2.c_str());
}
void* apManager=nullptr;
void (*apEnable)(void*,bool)=nullptr;
bool startAp(const std::string& hostname) {
  void* net=networkLibrary(); if(!net) return false;
  auto manager=reinterpret_cast<void*(*)()>(dlsym(net,"_ZN10NetManager11getInstanceEv"));
  auto start=reinterpret_cast<void(*)(void*)>(dlsym(net,"_ZN10NetManager5startEv"));
  auto getAp=reinterpret_cast<void*(*)(void*)>(dlsym(net,"_ZNK10NetManager16getSoftApManagerEv"));
  auto name=reinterpret_cast<void(*)(void*,const char*,const char*)>(dlsym(net,"_ZN13SoftApManager13setSsidAndPwdEPKcS1_"));
  apEnable=reinterpret_cast<void(*)(void*,bool)>(dlsym(net,"_ZN13SoftApManager9setEnableEb"));
  if(!manager || !start || !getAp || !name || !apEnable) return false;
  void* instance=manager(); if(!instance) return false;
  start(instance); apManager=getAp(instance); if(!apManager) return false;
  name(apManager,hostname.c_str(),""); apEnable(apManager,true); return true;
}
bool boundedDhcp(const std::atomic<bool>& stop) {
  pid_t child;
  char executable[]="/proc/self/exe",argument[]="--dhcp-once";
  char* args[]={executable,argument,nullptr};
  if(posix_spawn(&child,executable,nullptr,nullptr,args,environ)!=0) return false;
  for(int i=0;i<200 && !stop;++i) {
    int status;
    if(waitpid(child,&status,WNOHANG)==child) return WIFEXITED(status) && WEXITSTATUS(status)==0;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  kill(child,SIGKILL); waitpid(child,nullptr,0); return false;
}
bool syncTime(const std::string& server) {
  if(server.empty()) return false;
  addrinfo hints{}; hints.ai_family=AF_INET; hints.ai_socktype=SOCK_DGRAM;
  addrinfo* list=nullptr;
  if(getaddrinfo(server.c_str(),"123",&hints,&list)!=0) return false;
  int fd=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);
  bool ok=fd>=0 && connect(fd,list->ai_addr,list->ai_addrlen)==0;
  freeaddrinfo(list);
  if(!ok) { if(fd>=0) close(fd); return false; }
  std::array<uint8_t,48> request{},reply{}; request[0]=0x23;
  timeval current{}; gettimeofday(&current,nullptr);
  uint32_t seconds=htonl(static_cast<uint32_t>(current.tv_sec+2208988800ULL));
  uint32_t fraction=htonl(static_cast<uint32_t>((static_cast<uint64_t>(current.tv_usec)<<32)/1000000));
  std::memcpy(request.data()+40,&seconds,4); std::memcpy(request.data()+44,&fraction,4);
  ok=send(fd,request.data(),request.size(),0)==48;
  pollfd p{fd,POLLIN,0};
  ok=ok && poll(&p,1,1500)>0 && recv(fd,reply.data(),reply.size(),0)==48;
  close(fd);
  if(!ok || (reply[0]&7)!=4 || (reply[0]>>6)==3 || reply[1]==0 || reply[1]>15 ||
     std::memcmp(reply.data()+24,request.data()+40,8)!=0) return false;
  std::memcpy(&seconds,reply.data()+40,4); std::memcpy(&fraction,reply.data()+44,4);
  uint64_t epoch=ntohl(seconds);
  // NTP era rolls over in 2036; choose the era nearest the current system time.
  if(epoch<2208988800ULL) epoch+=1ULL<<32;
  timeval time{}; time.tv_sec=epoch-2208988800ULL;
  time.tv_usec=(static_cast<uint64_t>(ntohl(fraction))*1000000)>>32;
  return settimeofday(&time,nullptr)==0;
}
}
std::string wifiScan() { scanRequested=true; std::lock_guard<std::mutex> lock(stateMutex); return scanReady ? scanState : ""; }
int dhcpOnce() { return dhcp() ? 0 : 1; }
std::string wifiSsid() { std::lock_guard<std::mutex> lock(stateMutex); return ssidState; }
bool wifiConnected() { std::lock_guard<std::mutex> lock(stateMutex); return connectedState; }
bool wifiApMode() { return apMode; }
void resetWifi() { Wpa wpa; wpa.command("REMOVE_NETWORK all"); wpa.command("SAVE_CONFIG"); }
void System::stop() { stop_=true; wake_.notify_all(); if(worker_.joinable()) worker_.join(); }
System::~System() { stop(); }
void System::begin(const awtrix::DeviceConfig& cfg) {
  if(!hardwareEnabled()) return;
  worker_=std::thread([this,cfg] {
    const auto& ssid=cfg.wifiSsid; const auto& password=cfg.wifiPass; const auto& server=cfg.ntpServer;
    const auto hostname=awtrix::net::effectiveHostname(cfg.hostname,uid());
    char previousHostname[256]{}; gethostname(previousHostname,sizeof(previousHostname)-1);
    sethostname(hostname.c_str(),hostname.size());
    Mdns mdns; mdns.begin(hostname,cfg.webPort);
    const auto fingerprint=std::to_string(std::hash<std::string>{}(ssid+'\0'+password));
    const auto appliedPath=awtrix::sim::hostPath("/.wifi-applied");
    std::string applied;
    awtrix::sim::readFile(appliedPath,applied);
    bool configured=ssid.empty() || applied==fingerprint,wasConnected=false,initial=true,networkChanged=false;
    int scanDue=0,ntpDue=0,dhcpDue=0,disconnectedSeconds=0,roamDue=30;
    while(!stop_) {
      Wpa wpa;
      auto status=fields(wpa.command("STATUS"));
      bool connected=status["wpa_state"]=="COMPLETED";
      if(status.empty()) { int ignored=std::system("/bin/setprop ctl.start wpa_supplicant"); (void)ignored; }
      if(!configured && !status.empty()) {
        networkChanged=true;
        configured=setNetwork(wpa,ssid,password);
        if(configured) awtrix::sim::writeFile(appliedPath,fingerprint);
        if(!configured) std::fprintf(stderr,"TC002 Wi-Fi configuration failed\n");
      }
      { std::lock_guard<std::mutex> lock(stateMutex); ssidState=status["ssid"]; connectedState=connected; }
      if(connected && (!wasConnected || dhcpDue<=0)) {
        // Keep a valid lease inherited from the launcher; renew periodically.
        if(cfg.netStatic) staticNetwork(cfg);
        else if(!initial || networkChanged || ipAddress()=="127.0.0.1") boundedDhcp(stop_);
        initial=false; networkChanged=false;
        dhcpDue=1800;
      }
      if(connected) disconnectedSeconds=0;
      else ++disconnectedSeconds;
      if(!apMode && disconnectedSeconds>std::max<long>(15,cfg.wifiConnectTimeout/1000))
        apMode=startAp(hostname);
      if(connected && apMode && apEnable && apManager) { apEnable(apManager,false); apMode=false; }
      if(connected && cfg.wifiRoamRssi<0 && --roamDue<=0) {
        if(wifiRssi()<cfg.wifiRoamRssi) wpa.command("SCAN");
        roamDue=30;
      }
      mdns.tick();
      if(scanDue==0 && scanRequested.exchange(false)) { wpa.command("SCAN"); scanDue=3; }
      if(scanDue>0 && --scanDue==0) {
        auto result=scanJson(wpa.command("SCAN_RESULTS"));
        std::lock_guard<std::mutex> lock(stateMutex); scanState=std::move(result); scanReady=true;
      }
      if(connected && ntpDue<=0) {
        bool success=syncTime(server); ntpDue=success ? 3600 : 60;
        if(success) std::fprintf(stderr,"TC002 time synchronized\n");
      }
      wasConnected=connected; --ntpDue; --dhcpDue;
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait_for(lock,std::chrono::seconds(1),[this] { return stop_.load(); });
    }
    if(apMode && apEnable && apManager) { apEnable(apManager,false); apMode=false; }
    sethostname(previousHostname,std::strlen(previousHostname));
  });
}
}

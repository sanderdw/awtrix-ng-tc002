// This helper runs from /tmp and links only libraries on the root filesystem.
// It never erases a block until the complete image and live partition match.
#include "FirmwareImage.h"
#include "VendorFingerprints.h"
#include "VendorLibrary.h"
#include "VendorApplicationPath.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <mtd/mtd-user.h>
#include <string>
#include <vector>
#include <dirent.h>
#include <signal.h>
#include <sched.h>

// The stock GUI leaves this Bluetooth UART helper running after init stops it.
// Its executable is on /res, so it must exit before that filesystem can unmount.
// Match the actual executable path; never kill unrelated processes by name.
static void stopBluetoothHelper() {
  DIR* proc=opendir("/proc");
  if(!proc) return;
  while(auto* entry=readdir(proc)) {
    if(!*entry->d_name || std::strspn(entry->d_name,"0123456789")!=std::strlen(entry->d_name)) continue;
    char path[300],target[256]{};
    std::snprintf(path,sizeof(path),"/proc/%s/exe",entry->d_name);
    ssize_t length=readlink(path,target,sizeof(target)-1);
    if(length>0 && !std::strcmp(target,"/res/bin/hciattach")) {
      pid_t pid=std::strtol(entry->d_name,nullptr,10);
      if(pid>1 && kill(pid,SIGTERM)==0) {
        bool alive=true;
        for(int attempt=0;attempt<20;++attempt) {
          usleep(100000);
          length=readlink(path,target,sizeof(target)-1);
          if(length<0) { alive=false; break; }
          target[length]=0;
          if(std::strcmp(target,"/res/bin/hciattach")) { alive=false; break; }
        }
        // Some stock launches leave TERM ineffective. Recheck the executable
        // before forcing this one helper to exit; do not rely on a stale PID.
        if(alive) kill(pid,SIGKILL);
        std::printf("Terminated stock Bluetooth helper %d\n",int(pid));
      }
    }
  }
  closedir(proc);
}
// Refuse to replace a stock firmware the port does not recognise unless the operator says --force.
// libmi_ao.so and libzknet.so need a recorded hash: AWTRIX passes hand-measured structures to them.
// The vendor application may instead define every function the launcher calls into; without those
// the knob-hold and three-strikes fallbacks could not bring the stock app back.
static std::string joined(const std::vector<std::string>& names) {
  std::string out;
  for(const std::string& name:names) out+=(out.empty() ? "" : ", ")+name;
  return out;
}
static bool stockFirmwareRecognised() {
  bool ok=true;
  for(const tc002::VendorFile* f=tc002::kVendorFiles;f->library;++f) {
    const std::string path = !std::strcmp(f->library,"libulanzi-bootstrap.so")
      ? tc002::vendorApplicationPath() : f->path;
    const tc002::VendorCheck check=tc002::checkVendorFile(f->library,path);
    const char* digest=check.sha256.empty() ? "unreadable" : check.sha256.c_str();
    if(check.status==tc002::VendorStatus::Verified) {
      if(check.required.empty())
        std::printf("Stock firmware check: %s at %s verified\n",f->library,path.c_str());
      else if(check.missing.empty())
        std::printf("Stock firmware check: %s at %s verified; defines the launcher's %u entry points\n",
                    f->library,path.c_str(),unsigned(check.required.size()));
      else
        std::printf("Stock firmware check: %s at %s verified (symbol check disagrees: %s)\n",
                    f->library,path.c_str(),check.elfReadable ? joined(check.missing).c_str() : "not a readable ELF file");
    } else if(check.status==tc002::VendorStatus::Compatible) {
      std::printf("Stock firmware check: %s at %s is not a recorded build (sha256 %s) but defines every entry point the launcher uses\n",
                  f->library,path.c_str(),digest);
    } else {
      const std::string detail=check.required.empty() ? ""
        : check.elfReadable ? "; it does not define "+joined(check.missing) : "; not a readable ELF shared library";
      std::fprintf(stderr,"Stock firmware check: %s at %s (sha256 %s) is not a build this port was verified against (stock app %s, MCU %s)%s\n",
                   f->library,path.c_str(),digest,tc002::kStockApp,tc002::kStockMcu,detail.c_str());
      ok=false;
    }
  }
  return ok;
}
int main(int argc,char** argv) {
  const bool force=argc==4 && !std::strcmp(argv[3],"--force");
  if((argc!=3 && !force) || (std::strcmp(argv[1],"--validate") && std::strcmp(argv[1],"--preflight") && std::strcmp(argv[1],"--install"))) {
    std::fprintf(stderr,"Usage: tc002-update --validate|--preflight|--install update.img [--force]\n"); return 2;
  }
  int imageFd=open(argv[2],O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
  tc002::FirmwareImage image; std::string error;
  if(imageFd<0 || !tc002::validateFirmware(imageFd,image,error)) {
    std::fprintf(stderr,"Update rejected: %s\n",imageFd<0 ? "cannot open image" : error.c_str()); return 1;
  }
  if(!std::strcmp(argv[1],"--validate")) {
    std::printf("Valid TC002 res image: %u bytes\n",image.size); close(imageFd); return 0;
  }
#if !defined(__arm__) || defined(__aarch64__)
  std::fprintf(stderr,"Installation is only available on the TC002\n"); return 1;
#else
  char self[256]{}; ssize_t n=readlink("/proc/self/exe",self,sizeof(self)-1);
  if(n<0 || std::strncmp(self,"/tmp/",5)) {
    std::fprintf(stderr,"Copy this helper to /tmp before installing\n"); return 1;
  }
  FILE* proc=std::fopen("/proc/mtd","r"); char line[256]; bool matched=false;
  while(proc && std::fgets(line,sizeof(line),proc))
    if(!std::strcmp(line,"mtd3: 00800000 00010000 \"res\"\n")) matched=true;
  if(proc) std::fclose(proc);
  if(!matched) { std::fprintf(stderr,"Unexpected flash layout\n"); return 1; }
  int flash=open("/dev/mtd3",O_RDWR|O_SYNC|O_CLOEXEC);
  if(flash<0) flash=open("/dev/mtd/mtd3",O_RDWR|O_SYNC|O_CLOEXEC);
  mtd_info_user info{};
  if(flash<0 || ioctl(flash,MEMGETINFO,&info)!=0 || info.type!=MTD_NORFLASH ||
     info.size!=0x800000 || info.erasesize!=0x10000) {
    std::fprintf(stderr,"Unexpected res flash device\n"); return 1;
  }
  // AWTRIX isolates its DNS bind mount. Updates must unmount /res in init's
  // namespace, where the launcher lives, before erasing the underlying flash.
  int mountNamespace=open("/proc/1/ns/mnt",O_RDONLY|O_CLOEXEC);
  if(mountNamespace<0 || setns(mountNamespace,CLONE_NEWNS)!=0) {
    std::perror("Cannot enter system mount namespace; nothing erased");
    if(mountNamespace>=0) close(mountNamespace);
    close(flash); close(imageFd); return 1;
  }
  close(mountNamespace);
  if(!stockFirmwareRecognised()) {
    if(!force) { std::fprintf(stderr,"Refusing to install on an unverified stock firmware; nothing erased (add --force to override)\n"); close(flash); close(imageFd); return 1; }
    std::fprintf(stderr,"Continuing on an unverified stock firmware because of --force\n");
  }
  if(!std::strcmp(argv[1],"--preflight")) {
    std::puts("Preflight passed: valid image, matching 8 MiB NOR res partition; nothing written");
    close(flash); close(imageFd); return 0;
  }
  // Detach before asking init to stop the GUI. The web response has time to finish.
  setsid(); sleep(2);
  if(std::system("/bin/setprop ctl.stop zkswe")!=0) return 1;
  // setprop acknowledges the request before init has finished stopping the GUI.
  // During that interval the stock GUI can respawn hciattach.
  sleep(2);
  stopBluetoothHelper();
  sleep(2); sync();
  if(umount("/res")!=0) {
    std::perror("Cannot unmount /res; nothing erased");
    int ignored=std::system("/bin/setprop ctl.start zkswe"); (void)ignored; return 1;
  }
  std::array<unsigned char,65536> block{},verify{};
  for(uint32_t pos=0;pos<image.size;pos+=info.erasesize) {
    block.fill(0xff);
    size_t bytes=std::min<uint32_t>(info.erasesize,image.size-pos);
    if(pread(imageFd,block.data(),bytes,572+pos)!=ssize_t(bytes)) goto failed;
    if(pos==0) std::memcpy(block.data(),image.first,16);
    erase_info_user erase{pos,info.erasesize};
    if(ioctl(flash,MEMERASE,&erase)!=0 || pwrite(flash,block.data(),block.size(),pos)!=ssize_t(block.size()) ||
       pread(flash,verify.data(),verify.size(),pos)!=ssize_t(verify.size()) || block!=verify) goto failed;
  }
  fsync(flash); close(flash); close(imageFd); sync();
  std::puts("Firmware written and read-back verified; rebooting"); std::fflush(stdout);
  reboot(RB_AUTOBOOT); std::perror("Update verified; reboot manually"); return 1;
failed:
  std::perror("Flash write/verification failed; keep power on and use ADB recovery");
  return 1;
#endif
}

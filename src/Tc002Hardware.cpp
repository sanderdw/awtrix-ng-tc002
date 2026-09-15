#include "Tc002Hardware.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <linux/input.h>
#include <linux/spi/spidev.h>
#include <termios.h>
#include <cstdio>
#include <cstdlib>

namespace tc002 {
namespace { bool enabled = false; Hardware* active = nullptr; }
bool batteryAvailable() { return active && active->batteryAvailable(); }
int batteryPercent() { return active ? active->batteryPercent() : -1; }
const char* caCertPath() {
  const char* path=std::getenv("AWTRIX_CA_CERT");
  return path && *path ? path : "/res/etc/cacert.pem";
}
bool hardwareEnabled() { return enabled; }
void setHardwareEnabled(bool v) { enabled = v; }
std::vector<uint8_t> mcuPacket(uint8_t command, const std::vector<uint8_t>& payload) {
  if(payload.size()>32) throw std::invalid_argument("MCU payload too large");
  std::vector<uint8_t> out{0xff,0x55,command,static_cast<uint8_t>(payload.size())};
  out.insert(out.end(),payload.begin(),payload.end());
  uint16_t sum=0; for(auto b:out) sum+=b;
  out.push_back(sum>>8); out.push_back(sum&255); return out;
}
bool parseMcuPacket(std::vector<uint8_t>& b, uint8_t& cmd, std::vector<uint8_t>& payload) {
  while(b.size()>=6) {
    if(b[0]!=0xff || b[1]!=0x55 || b[3]>32) { b.erase(b.begin()); continue; }
    size_t n=b[3]+6;
    if(b.size()<n) return false;
    uint16_t sum=0; for(size_t i=0;i<n-2;++i) sum+=b[i];
    if(sum!=static_cast<uint16_t>((b[n-2]<<8)|b[n-1])) { b.erase(b.begin()); continue; }
    cmd=b[2]; payload.assign(b.begin()+4,b.begin()+n-2); b.erase(b.begin(),b.begin()+n);
    return true;
  }
  return false;
}
Frame packFrame(const uint32_t* pixels, bool mirror, bool rotate) {
  Frame out{};
  for (int y=0; y<Height; ++y) for (int x=0; x<Width; ++x) {
    int sx = mirror ? Width-1-x : x, sy = y;
    if (rotate) { sx = Width-1-sx; sy = Height-1-sy; }
    uint32_t c = pixels[sy*Width+sx];
    auto p = (y*Stride+x)*3;
    out[p] = c >> 16; out[p+1] = c >> 8; out[p+2] = c;
  }
  return out;
}
std::string uid() {
  for (auto dev : {"wlan0", "eth0"}) {
    std::ifstream f(std::string("/sys/class/net/")+dev+"/address");
    std::string mac; f >> mac;
    mac.erase(std::remove(mac.begin(),mac.end(),':'),mac.end());
    if (mac.size()==12 && mac!="000000000000") return mac;
  }
  return "tc002-host";
}
std::string ipAddress() {
  int fd=socket(AF_INET,SOCK_DGRAM,0);
  if (fd<0) return "0.0.0.0";
  std::string result="127.0.0.1";
  for (auto dev : {"wlan0", "eth0"}) {
    ifreq req{}; std::strncpy(req.ifr_name,dev,IFNAMSIZ-1);
    if (ioctl(fd,SIOCGIFADDR,&req)==0) {
      result=inet_ntoa(reinterpret_cast<sockaddr_in*>(&req.ifr_addr)->sin_addr); break;
    }
  }
  close(fd); return result;
}
long availableMemory() {
  std::ifstream f("/proc/meminfo"); std::string key,unit; long n;
  while(f>>key>>n>>unit) if(key=="MemAvailable:") return n*1024;
  return 0;
}
int wifiRssi() {
  std::ifstream f("/proc/net/wireless"); std::string line;
  while(std::getline(f,line)) {
    unsigned int status; float quality,level;
    if(std::sscanf(line.c_str()," wlan0: %x %f %f",&status,&quality,&level)==3)
      return static_cast<int>(level);
  }
  return 0;
}
Hardware::~Hardware() {
  if(active==this) active=nullptr;
  for (int fd : {spi_,gpio_,uart_,inputs_[0],inputs_[1]}) if(fd>=0) close(fd);
}
void Hardware::begin() {
  if(!enabled) return;
  active=this;
  uart_=open("/dev/ttyS1",O_RDWR|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);
  if(uart_<0) throw std::runtime_error("TC002 MCU UART unavailable");
  termios t{};
  if(tcgetattr(uart_,&t)<0) throw std::runtime_error("MCU termios unavailable");
  cfmakeraw(&t); cfsetispeed(&t,B1500000); cfsetospeed(&t,B1500000);
  t.c_cflag |= CLOCAL|CREAD; t.c_cflag &= ~CRTSCTS;
  if(tcsetattr(uart_,TCSANOW,&t)<0) throw std::runtime_error("MCU UART configuration failed");
  // The TC002 requires this handshake before accepting its first LED frame.
  for(int attempt=0;attempt<3 && mcuVersion_.empty();++attempt) {
    query(0x11);
    for(int i=0;i<25 && mcuVersion_.empty();++i) { usleep(20000); tickMcu(); }
  }
  if(mcuVersion_.empty()) throw std::runtime_error("TC002 MCU did not answer version handshake");
  std::fprintf(stderr,"TC002 MCU %s\n",mcuVersion_.c_str());
  // GPIO number and polarity follow Ulanzi's PageBase::sendLedData.
  if(access("/sys/class/gpio/gpio35/value",F_OK)!=0) {
    std::ofstream exportPin("/sys/class/gpio/export"); exportPin<<35;
  }
  // Match the verified vendor sequence: set direction and level together on
  // every edge. A value-only driver with different SPI setup corrupted frames.
  gpio_=open("/sys/class/gpio/gpio35/direction",O_WRONLY|O_CLOEXEC);
  spi_=open("/dev/spidev0.0",O_RDWR|O_CLOEXEC);
  if(gpio_<0 || spi_<0) throw std::runtime_error("TC002 SPI/GPIO unavailable");
  uint8_t mode=SPI_MODE_0,bits=8,lsb=0; uint32_t speed=10000000;
  if(ioctl(spi_,SPI_IOC_WR_MODE,&mode)<0 || ioctl(spi_,SPI_IOC_RD_MODE,&mode)<0 ||
     ioctl(spi_,SPI_IOC_WR_BITS_PER_WORD,&bits)<0 || ioctl(spi_,SPI_IOC_RD_BITS_PER_WORD,&bits)<0 ||
     ioctl(spi_,SPI_IOC_WR_LSB_FIRST,&lsb)<0 || ioctl(spi_,SPI_IOC_WR_MAX_SPEED_HZ,&speed)<0 ||
     ioctl(spi_,SPI_IOC_RD_MAX_SPEED_HZ,&speed)<0 || mode!=0 || bits!=8 || speed!=10000000)
    throw std::runtime_error("TC002 SPI configuration failed");
  for(int i=0;i<2;++i) {
    auto path=std::string("/dev/input/event")+std::to_string(67+i);
    inputs_[i]=open(path.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    if(inputs_[i]<0) throw std::runtime_error("TC002 input unavailable: "+path);
  }
}
void Hardware::show(const uint32_t* pixels, bool mirror, bool rotate) {
  if(!enabled) return;
  auto now=std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
  if(now-lastFrameUs_<15000) return;
  auto data=packFrame(pixels,mirror,rotate);
  if(lseek(gpio_,0,SEEK_SET)<0 || write(gpio_,"low",3)!=3)
    throw std::runtime_error("TC002 GPIO handshake failed");
  usleep(1000);
  auto n=write(spi_,data.data(),data.size());
  // Firmware 1.1.1 holds GPIO_35 low for another millisecond after write()
  // returns, before latching the frame. The published demo omits this delay.
  usleep(1000);
  lseek(gpio_,0,SEEK_SET);
  if(write(gpio_,"high",4)!=4 || n!=static_cast<ssize_t>(data.size()))
    throw std::runtime_error("TC002 SPI frame failed");
  // Leave a full 15 ms idle period after the latch, including on slow writes.
  lastFrameUs_=std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
}
void Hardware::poll(bool& left,bool& select,bool& right,int& rotation) {
  rotation=0;
  if(!enabled) return;
  tickMcu();
  for(int fd:inputs_) {
    input_event ev{};
    while(read(fd,&ev,sizeof(ev))==sizeof(ev)) {
      if(ev.type==EV_KEY && (ev.value==0 || ev.value==1)) {
        switch(ev.code) {
          case 0x6c: keys_[0]=ev.value; break;
          case 0x69: keys_[1]=ev.value; break;
          case 0x6a: keys_[2]=ev.value; break;
          case 0x67: keys_[3]=ev.value; break;
        }
      }
      if(ev.type==EV_ABS) {
        if(lastRotation_==8 && ev.value==1) ++rotation;
        if(lastRotation_==13 && ev.value==11) --rotation;
        lastRotation_=ev.value;
      }
    }
  }
  left=keys_[0]; select=keys_[1]||keys_[3]; right=keys_[2];
}
void Hardware::query(uint8_t cmd) {
  auto p=mcuPacket(cmd);
  if(write(uart_,p.data(),p.size())!=static_cast<ssize_t>(p.size()))
    throw std::runtime_error("MCU request failed");
}
void Hardware::tickMcu() {
  if(uart_<0) return;
  uint8_t bytes[128]; ssize_t n;
  while((n=read(uart_,bytes,sizeof(bytes)))>0) {
    rx_.insert(rx_.end(),bytes,bytes+n);
    uint8_t cmd; std::vector<uint8_t> data;
    while(parseMcuPacket(rx_,cmd,data)) {
      if(cmd==0x11) mcuVersion_.assign(data.begin(),data.end());
      if(cmd==0x03 && data.size()>=3 && data[0]<=100) {
        batteryMv_=(data[1]<<8)|data[2];
        batteryPercent_=data[0];
      }
    }
    if(rx_.size()>256) rx_.clear();
  }
  auto now=std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
  if(now-lastBatteryMs_>=10000) { query(0x03); lastBatteryMs_=now; }
}
}

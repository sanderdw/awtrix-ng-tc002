// Temporary diagnostic, never installed in the firmware image.
// Compare our RGB packing with the SPI/GPIO helpers already installed on TC002.
#include "Tc002Hardware.h"
#include <dlfcn.h>
#include <cstdio>
#include <stdexcept>
#include <cstdlib>
#include <unistd.h>
int main(int argc,char** argv) {
  int seconds=argc>1 ? std::atoi(argv[1]) : 90;
  tc002::setHardwareEnabled(true);
  tc002::Hardware hardware;
  hardware.begin();
  dlopen("/lib/libcutils.so",RTLD_NOW|RTLD_GLOBAL);
  for(auto name : {"libz.so.1","libpng12.so.0","libjpeg.so.9","libfreetype.so.6",
                   "liblog.so","libnanovg.so","libmi_sys.so","libmi_gfx.so"})
    dlopen(name,RTLD_LAZY|RTLD_GLOBAL);
  if(!dlopen("/lib/libeasyui.so",RTLD_LAZY|RTLD_GLOBAL)) {
    std::fprintf(stderr,"%s\n",dlerror()); return 1;
  }
  void* lib=dlopen("/lib/libzkhardware.so",RTLD_NOW|RTLD_LOCAL);
  if(!lib) { std::fprintf(stderr,"%s\n",dlerror()); return 1; }
  auto ctor=reinterpret_cast<void(*)(void*,int,unsigned char,unsigned,unsigned char,bool)>(
    dlsym(lib,"_ZN9SpiHelperC1Eihjhb"));
  auto send=reinterpret_cast<bool(*)(void*,const unsigned char*,unsigned)>(
    dlsym(lib,"_ZN9SpiHelper5writeEPKhj"));
  auto gpio=reinterpret_cast<int(*)(const char*,int)>(
    dlsym(lib,"_ZN10GpioHelper6outputEPKci"));
  auto destroy=reinterpret_cast<void(*)(void*)>(dlsym(lib,"_ZN9SpiHelperD1Ev"));
  if(!ctor || !send || !gpio || !destroy) return 2;
  // Installed ARM SDK object has a vtable and one spidev pointer (8 bytes).
  alignas(8) unsigned char spi[64]{};
  ctor(spi,0,0,10000000,8,false);
  uint32_t pixels[52*16];
  for(int y=0;y<16;++y) for(int x=0;x<52;++x)
    pixels[y*52+x]=(y==0 || y==15 || x==0 || x==51) ? 0x808080 :
      x<17 ? 0x800000 : x<34 ? 0x008000 : 0x000080;
  auto data=tc002::packFrame(pixels,false,false);
  std::puts("SDK SPI calibration: red / green / blue, white border, 90 seconds");
  std::fflush(stdout);
  for(int i=0;i<seconds*10;++i) {
    gpio("GPIO_35",0); usleep(1000);
    if(!send(spi,data.data(),data.size())) return 3;
    usleep(1000); gpio("GPIO_35",1); usleep(98000);
  }
  destroy(spi);
}

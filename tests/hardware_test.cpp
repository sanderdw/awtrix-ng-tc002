#include "Tc002Hardware.h"
#include "InputDevices.h"
#include <cstdio>
#include <cstdlib>
void check(bool b) { if(!b) std::abort(); }
int main() {
  uint32_t pixels[52*16]{};
  pixels[0]=0xff0102; pixels[51]=0x030405;
  pixels[15*52]=0x060708; pixels[831]=0x090a0b;
  auto f=tc002::packFrame(pixels,false,false);
  check(f.size()==3072 && f[0]==255 && f[1]==1 && f[2]==2);
  check(f[51*3]==3 && f[(15*64+51)*3]==9);
  for(int y=0;y<16;++y) for(int x=52*3;x<64*3;++x) check(f[y*192+x]==0);
  check(tc002::packFrame(pixels,true,false)[0]==3);
  check(tc002::packFrame(pixels,false,true)[0]==9);
  check(tc002::packFrame(pixels,true,true)[0]==6);
  auto query=tc002::mcuPacket(0x11);
  check(query==std::vector<uint8_t>({0xff,0x55,0x11,0,1,0x65}));
  auto packet=tc002::mcuPacket(3,{88,0x10,0x04});
  std::vector<uint8_t> stream{0,1,2,0xff}, data; uint8_t command=0;
  stream.insert(stream.end(),packet.begin(),packet.end()-1);
  check(!tc002::parseMcuPacket(stream,command,data));
  stream.push_back(packet.back());
  check(tc002::parseMcuPacket(stream,command,data) && command==3 && data.size()==3);
  packet.back()^=1; stream=packet;
  stream.insert(stream.end(),query.begin(),query.end());
  check(tc002::parseMcuPacket(stream,command,data) && command==0x11);
  {
    unsigned char ev[TC002_INPUT_BITS(EV_MAX+1)]{},key[TC002_INPUT_BITS(KEY_MAX+1)]{},abs[TC002_INPUT_BITS(ABS_MAX+1)]{};
    auto set=[](unsigned char* bits,unsigned bit) { bits[bit/8]|=1<<(bit%8); };
    check(tc002ClassifyInput(ev,key,abs)==0);
    set(ev,EV_KEY); set(key,KEY_LEFT); set(key,KEY_RIGHT);
    check(tc002ClassifyInput(ev,key,abs)==1);
    set(key,BTN_TOUCH);
    check(tc002ClassifyInput(ev,key,abs)==0);                 // a touch panel is never a button device
    unsigned char ev2[TC002_INPUT_BITS(EV_MAX+1)]{},key2[TC002_INPUT_BITS(KEY_MAX+1)]{},abs2[TC002_INPUT_BITS(ABS_MAX+1)]{};
    set(ev2,EV_ABS);
    check(tc002ClassifyInput(ev2,key2,abs2)==2);              // the rotary encoder reports EV_ABS
    set(abs2,ABS_MT_POSITION_X);
    check(tc002ClassifyInput(ev2,key2,abs2)==0);              // multitouch is not the encoder
    int fds[2]; check(tc002OpenInputDevices(fds,2,O_RDONLY|O_NONBLOCK|O_CLOEXEC)>=0);
    check(tc002SelectHeld(fds,0)==0);
  }
  std::puts("TC002 frame geometry, RGB, padding, orientation and input classification passed");
}

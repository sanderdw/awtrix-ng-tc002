#include "Tc002Hardware.h"
#include "InputDevices.h"
#include "Sha256.h"
#include <string>
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
  {
    tc002::Sha256 empty; check(empty.hex()=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    tc002::Sha256 abc; abc.update("abc",3);
    check(abc.hex()=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    tc002::Sha256 two; two.update("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",56);
    check(two.hex()=="248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    tc002::Sha256 million; std::string a(1000,'a'); for(int i=0;i<1000;++i) million.update(a.data(),a.size());
    check(million.hex()=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  }
  std::puts("TC002 frame geometry, RGB, padding, orientation, input classification and SHA-256 passed");
}

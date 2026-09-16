#include "Tc002Audio.h"
#include "Tc002Hardware.h"
#include "VendorLibrary.h"
#include "core/audio/Mp3FileDecoder.h"
#include "core/radio/IcyStream.h"
#include "core/radio/IcyMetadata.h"
#include "core/radio/PlaylistParser.h"
#include "core/radio/RadioDisplay.h"
#include "core/sound/Rtttl.h"
#include "core/sound/SoundMp3.h"
#include "sim/SimStore.h"
#include "sim/vendor/httplib.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <memory>
#include <vector>

namespace tc002 {
using namespace awtrix;
Audio::~Audio() { stopAll(); }
void Audio::begin() {
  if(!hardwareEnabled() || library_) return;
#if defined(__arm__) && !defined(__aarch64__)
  // These C entry points belong to the MI audio driver already installed in
  // TC002 system firmware. No vendor library is copied into this repository.
  for(auto name : {"liblog.so","libcam_os_wrapper.so","libmi_sys.so"})
    if(!dlopen(name,RTLD_LAZY|RTLD_GLOBAL)) {
      std::fprintf(stderr,"TC002 audio dependency: %s\n",dlerror()); return;
    }
  // Only the exact driver build the structure layouts below were measured against is used.
  library_=openTrustedVendorLibrary("libmi_ao.so",RTLD_NOW|RTLD_LOCAL);
  if(!library_) return;
#define LOAD(member,name) member=reinterpret_cast<decltype(member)>(dlsym(library_,name))
  LOAD(setAttr_,"MI_AO_SetPubAttr"); LOAD(enable_,"MI_AO_Enable");
  LOAD(enableChannel_,"MI_AO_EnableChn"); LOAD(disable_,"MI_AO_Disable");
  LOAD(disableChannel_,"MI_AO_DisableChn"); LOAD(send_,"MI_AO_SendFrame");
  LOAD(volume_,"MI_AO_SetVolume"); LOAD(mute_,"MI_AO_SetMute");
  LOAD(query_,"MI_AO_QueryChnStat");
#undef LOAD
  if(!setAttr_ || !enable_ || !enableChannel_ || !disable_ || !disableChannel_ ||
     !send_ || !volume_ || !mute_) { dlclose(library_); library_=nullptr; }
  if(library_) {
    auto init=reinterpret_cast<int(*)()>(dlsym(library_,"MI_SYS_Init"));
    if(init && init()!=0) { dlclose(library_); library_=nullptr; }
  }
#endif
}
void Audio::fail(const std::string& s) {
  std::lock_guard<std::mutex> lock(messageMutex_); error_=s;
  std::fprintf(stderr,"TC002 audio: %s\n",s.c_str());
}
bool Audio::configure(int sampleRate) {
  if(rate_==sampleRate) return true;
  closeOutput();
  // MI_AUDIO_Attr as used by the installed TC002 1.1.1 audio wrapper:
  // 52 bytes, 16-bit mono PCM, four 1024-sample frames, one output channel.
  std::array<uint32_t,13> attr{};
  attr[0]=sampleRate; attr[4]=4; attr[5]=1024; attr[7]=1;
  if(setAttr_(0,attr.data())!=0 || enable_(0)!=0 || enableChannel_(0,0)!=0) {
    disableChannel_(0,0); disable_(0); fail("speaker initialization failed"); return false;
  }
  rate_=sampleRate;
  volume_(0,0,-10,0); mute_(0,0,0);
  return true;
}
void Audio::closeOutput() {
  if(!rate_) return;
  // Let the final queued samples reach the speaker. Cancellation still stops
  // immediately; a broken driver cannot hold shutdown indefinitely.
  for(int i=0;query_ && !cancel_ && i<100;++i) {
    std::array<uint32_t,3> state{};
    if(query_(0,0,state.data())!=0 || state[2]==0) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  disableChannel_(0,0); disable_(0); rate_=0;
}
bool Audio::output(const int16_t* input,int frames,int channels) {
  if(cancel_ || frames<1 || channels<1 || channels>2) return false;
  std::vector<int16_t> pcm(frames);
  int gain=mode_==Tone ? toneVolume_.load() : mode_==Mp3 ? soundVolume_.load() : streamVolume_.load();
  for(int i=0;i<frames;++i) {
    int value=input[i*channels];
    if(channels==2) value=(value+input[i*channels+1])/2;
    pcm[i]=static_cast<int16_t>(value*std::min(gain,100)/100);
  }
  // The installed 32-bit MI_AUDIO_Frame ABI is 288 bytes. Pointer 0 lives at
  // byte 8, and its byte count at 84. Keep this isolated from host structures.
  alignas(8) std::array<unsigned char,288> frame{};
  uintptr_t pointer=reinterpret_cast<uintptr_t>(pcm.data());
  uint32_t bytes=frames*sizeof(int16_t);
  std::memcpy(frame.data()+8,&pointer,sizeof(pointer));
  std::memcpy(frame.data()+84,&bytes,sizeof(bytes));
  for(int attempt=0;attempt<20 && !cancel_;++attempt) {
    int result=send_(0,0,frame.data(),50);
    if(result==0) return true;
    // Only the driver's output-buffer-full code can be retried.
    if(static_cast<uint32_t>(result)!=0xa005200du) break;
  }
  if(!cancel_) fail("speaker output failed");
  return false;
}
void Audio::stopAll() {
  cancel_=true;
  if(worker_.joinable()) worker_.join();
  playing_=false; mode_=Idle;
}
void Audio::launch(Mode mode,std::function<void()> fn) {
  stopAll(); cancel_=false; mode_=mode; playing_=true;
  { std::lock_guard<std::mutex> lock(messageMutex_); error_.clear(); title_.clear(); }
  worker_=std::thread([this,fn=std::move(fn)] {
    try { fn(); } catch(const std::exception& e) { fail(e.what()); }
    closeOutput(); playing_=false;
  });
}
bool Audio::playRtttl(const std::string& text) {
  auto parsed=rtttl::parse(text);
  if(!available() || !parsed.ok) return false;
  launch(Tone,[this,parsed=std::move(parsed)] {
    constexpr int rate=48000;
    if(!configure(rate)) return;
    for(const auto& note:parsed.notes) {
      unsigned remaining=rtttl::noteMs(note.duration,parsed.timeUnit)*48;
      unsigned sample=0;
      while(remaining && !cancel_) {
        std::array<int16_t,480> pcm{};
        unsigned count=std::min<unsigned>(remaining,pcm.size());
        for(unsigned i=0;i<count;++i,++sample)
          pcm[i]=note.frequency ? static_cast<int16_t>(6000*std::sin(6.28318530718*note.frequency*sample/rate)) : 0;
        if(!output(pcm.data(),count,1)) return;
        remaining-=count;
      }
    }
  });
  return true;
}
bool Audio::playMelodyFile(const std::string& name) {
  std::string source;
  return rtttl::validName(name) && sim::readFile(sim::hostPath("/MELODIES/"+name+".txt"),source)
    && playRtttl(source);
}
bool Audio::playMp3(const std::string& path) {
  if(!available()) return false;
  const auto host=sim::hostPath(path);
  FILE* f=std::fopen(host.c_str(),"rb"); if(!f) return false;
  std::fclose(f);
  launch(Mp3,[this,host] {
    std::unique_ptr<FILE,decltype(&std::fclose)> file(std::fopen(host.c_str(),"rb"),std::fclose);
    if(!file) { fail("MP3 file could not be opened"); return; }
    auto decoder=std::make_unique<mp3::Decoder>();
    mp3::Mp3FileDecoder reader(*decoder);
    std::array<int16_t,mp3::kMaxPcmPerFrame> pcm{};
    while(!cancel_) {
      mp3::DecodeResult result;
      auto step=reader.next([](void*ctx,uint8_t*p,size_t n) { return static_cast<int>(std::fread(p,1,n,static_cast<FILE*>(ctx))); },file.get(),pcm.data(),result);
      if(step==mp3::Mp3FileDecoder::Step::Done) break;
      if(step==mp3::Mp3FileDecoder::Step::Error) { fail("MP3 decoding failed"); break; }
      if(!configure(result.sampleRateHz) || !output(pcm.data(),result.samples,result.channels)) break;
    }
  });
  name_=sound::mp3NameFor(path); return true;
}
DispatchResult Audio::playStream(const std::string& url,const std::string& label,DispatchDetail&) {
  if(!available()) return DispatchResult::Unavailable;
  launch(Radio,[this,url] {
    std::string streamUrl=url;
    if(radio::kindFromUrl(url)!=radio::PlaylistKind::None) {
      radio::Url playlist;
      if(!radio::parseUrl(url,playlist)) { fail("invalid playlist URL"); return; }
      httplib::Client list(std::string(playlist.tls?"https://":"http://")+playlist.host+":"+std::to_string(playlist.port));
      list.set_ca_cert_path(caCertPath()); list.set_connection_timeout(3);
      list.set_read_timeout(2); list.set_follow_location(true);
      std::string body;
      auto result=list.Get(playlist.path,[&](const char* p,size_t n) {
        if(cancel_ || body.size()+n>32768) return false;
        body.append(p,n); return true;
      });
      if(!result || result->status!=200 || !radio::parsePlaylist(body,streamUrl)) {
        if(!cancel_) fail("radio playlist could not be resolved");
        return;
      }
    }
    radio::Url parsed;
    if(!radio::parseUrl(streamUrl,parsed)) { fail("invalid radio URL"); return; }
    httplib::Client client(std::string(parsed.tls?"https://":"http://")+parsed.host+":"+std::to_string(parsed.port));
    client.set_ca_cert_path(caCertPath());
    client.set_connection_timeout(3); client.set_read_timeout(1); client.set_follow_location(true);
    radio::MetadataSplitter splitter; radio::TitleTracker titles;
    auto decoder=std::make_unique<mp3::Decoder>();
    std::array<int16_t,mp3::kMaxPcmPerFrame> pcm{};
    std::vector<uint8_t> encoded;
    bool good=true,decoded=false;
    auto result=client.Get(parsed.path,{{"Icy-MetaData","1"}},[&](const httplib::Response& response) {
      splitter.reset(std::atoi(response.get_header_value("icy-metaint").c_str()));
      return response.status==200 && !cancel_;
    },[&](const char* data,size_t n) {
      if(cancel_) return false;
      splitter.feed(reinterpret_cast<const uint8_t*>(data),n,[&](const uint8_t* p,size_t bytes) {
        encoded.insert(encoded.end(),p,p+bytes);
        size_t consumed=0;
        while(consumed<encoded.size() && !cancel_) {
          auto frame=decoder->decode(encoded.data()+consumed,encoded.size()-consumed,pcm.data());
          if(frame.status==mp3::DecodeStatus::NeedMoreData) break;
          if(frame.status==mp3::DecodeStatus::Ok) decoded=true;
          consumed+=std::max<size_t>(1,frame.bytesConsumed);
          if(frame.status==mp3::DecodeStatus::Ok &&
             (!configure(frame.sampleRateHz) || !output(pcm.data(),frame.samples,frame.channels))) { good=false; break; }
        }
        encoded.erase(encoded.begin(),encoded.begin()+std::min(consumed,encoded.size()));
        if(encoded.size()>65536) { fail("radio decoder buffer exceeded"); good=false; }
      },[&](const std::string& block) {
        if(titles.update(block)) { std::lock_guard<std::mutex> lock(messageMutex_); title_=titles.title(); }
      });
      return good && !cancel_;
    });
    if(!cancel_ && (!result || result->status!=200)) fail("radio connection failed");
    else if(!cancel_ && !decoded) fail("radio response contained no decodable MP3 audio");
  });
  name_=label; return DispatchResult::Ok;
}
void Audio::tick(int64_t nowMs) {
  if(!engine_) return;
  auto& runtime=engine_->state().runtime();
  bool mp3=mp3Playing(),radio=mode_==Radio && playing_;
  std::string error,title;
  { std::lock_guard<std::mutex> lock(messageMutex_); error=error_; title=title_; }
  if(!title.empty() && runtime.radioTitle!=title && engine_->state().settings().radioMeta) {
    AppSpec spec;
    if(radio::buildAnnouncement(title,radio::Announcement::Title,spec))
      engine_->notifications().push(spec,nowMs);
  }
  if(runtime.mp3Playing!=mp3 || runtime.radioPlaying!=radio || runtime.radioTitle!=title || runtime.radioError!=error) {
    runtime.mp3Playing=mp3; runtime.mp3Name=mp3 ? name_ : "";
    runtime.radioPlaying=radio; runtime.radioTitle=title; runtime.radioError=error;
    engine_->state().emit(StateEvent::RadioChanged);
  }
}
}

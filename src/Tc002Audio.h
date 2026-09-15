#pragma once
#include "core/CoreEngine.h"
#include "core/sound/AudioSinks.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace tc002 {
class Audio : public awtrix::sound::IToneSink, public awtrix::sound::IPcmSink {
 public:
  ~Audio();
  void begin() override;
  bool available() const { return library_ != nullptr; }
  void attach(awtrix::CoreEngine& engine) { engine_=&engine; }
  void setVolume(uint8_t value) override { toneVolume_=value; }
  void setSoundVolume(uint8_t value) override { soundVolume_=value; }
  void setStreamVolume(uint8_t value) override { streamVolume_=value; }
  bool playRtttl(const std::string&) override;
  bool playMelodyFile(const std::string&) override;
  void stop() override { if(mode_==Tone) stopAll(); }
  void stopAll();
  void tick() override {}
  bool isPlaying() const override { return mode_==Tone && playing_; }
  bool playMp3(const std::string&) override;
  void stopMp3() override { if(mode_==Mp3) stopAll(); }
  bool mp3Playing() const override { return mode_==Mp3 && playing_; }
  awtrix::DispatchResult playStream(const std::string&,const std::string&,awtrix::DispatchDetail&) override;
  void stopStream() override { if(mode_==Radio) stopAll(); }
  void tick(int64_t) override;
 private:
  enum Mode { Idle, Tone, Mp3, Radio };
  std::atomic<Mode> mode_{Idle};
  std::atomic<bool> playing_{false},cancel_{false};
  std::atomic<uint8_t> toneVolume_{80},soundVolume_{80},streamVolume_{50};
  std::thread worker_;
  std::mutex messageMutex_;
  std::string error_,title_,name_;
  awtrix::CoreEngine* engine_=nullptr;
  void* library_=nullptr;
  void launch(Mode,std::function<void()>);
  bool configure(int sampleRate);
  bool output(const int16_t*,int frames,int channels);
  void closeOutput();
  void fail(const std::string&);
  int rate_=0;
  int (*setAttr_)(int,void*)=nullptr;
  int (*enable_)(int)=nullptr;
  int (*enableChannel_)(int,int)=nullptr;
  int (*disable_)(int)=nullptr;
  int (*disableChannel_)(int,int)=nullptr;
  int (*send_)(int,int,void*,int)=nullptr;
  int (*volume_)(int,int,int,int)=nullptr;
  int (*mute_)(int,int,int)=nullptr;
  int (*query_)(int,int,void*)=nullptr;
};
}

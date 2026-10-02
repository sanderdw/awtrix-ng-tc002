#include "Tc002Mp3Probe.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include "core/audio/Mp3Frame.h"

namespace tc002 {
namespace {

using awtrix::mp3::FrameHeader;
using awtrix::mp3::Version;

// How far past the tag a first frame may start; encoders put it right after the tag.
constexpr std::size_t kScanBytes = 64 * 1024;
const char* const kReencode = "; re-encode it as MPEG-1 Layer III at 32, 44.1 or 48 kHz";

// ID3v2 header: "ID3", version, flags, then a 28-bit size with the top bit of each byte clear.
long id3v2Size(const uint8_t* h) {
  if (h[0] != 'I' || h[1] != 'D' || h[2] != '3') return 0;
  if ((h[6] | h[7] | h[8] | h[9]) & 0x80) return 0;
  const long body = (long(h[6]) << 21) | (long(h[7]) << 14) | (long(h[8]) << 7) | long(h[9]);
  return 10 + body + ((h[5] & 0x10) ? 10 : 0);
}

std::string unsupported(const FrameHeader& h) {
  const char* name = h.version == Version::Mpeg2 ? "MPEG-2" : "MPEG-2.5";
  return std::string(name) + " audio at " + std::to_string(h.sampleRateHz) + " Hz is not supported" +
         kReencode;
}

}

Mp3Probe probeMp3(const std::string& path) {
  Mp3Probe probe;
  std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(path.c_str(), "rb"), std::fclose);
  if (!file) {
    probe.problem = "the MP3 file could not be opened";
    return probe;
  }
  uint8_t tag[10];
  if (std::fread(tag, 1, sizeof tag, file.get()) == sizeof tag) probe.audioOffset = id3v2Size(tag);
  if (std::fseek(file.get(), probe.audioOffset, SEEK_SET) != 0) {
    probe.problem = "no MP3 audio frames found";
    return probe;
  }
  std::vector<uint8_t> buf(kScanBytes + 8 * 1024);
  buf.resize(std::fread(buf.data(), 1, buf.size(), file.get()));
  const std::size_t n = buf.size();

  // A frame counts once the next header follows it, or it ends the file, since 0xFF sync bytes
  // also turn up inside tags and audio data.
  bool freeFormat = false;
  std::size_t at = 0;
  for (std::size_t from = 0; from < kScanBytes && awtrix::mp3::findSync(buf.data(), n, from, at);
       from = at + 1) {
    FrameHeader h, next;
    if (!awtrix::mp3::parseHeader(buf.data() + at, n - at, h)) continue;
    const std::size_t len = static_cast<std::size_t>(h.frameBytes());
    if (len == 0) {
      freeFormat = true;
      continue;
    }
    const bool confirmed = at + len == n ||
        (at + len < n && awtrix::mp3::parseHeader(buf.data() + at + len, n - at - len, next) &&
         next.version == h.version && next.sampleRateHz == h.sampleRateHz);
    if (!confirmed) continue;
    if (!awtrix::mp3::isSupported(h)) probe.problem = unsupported(h);
    return probe;
  }
  // Layer I and II share the sync word but the shared parser only accepts Layer III.
  if (n >= 4 && buf[0] == 0xFF && (buf[1] & 0xE0) == 0xE0 && ((buf[1] >> 3) & 0x03) != 1 &&
      ((buf[1] >> 1) & 0x03) >= 2 && (buf[2] >> 4) != 15 && ((buf[2] >> 2) & 0x03) != 3)
    probe.problem = std::string("MPEG Layer I/II audio (MP1/MP2) is not supported") + kReencode;
  else if (freeFormat)
    probe.problem = std::string("free-format MP3 bitrate is not supported") + kReencode;
  else
    probe.problem = "no MP3 audio frames found";
  return probe;
}

}

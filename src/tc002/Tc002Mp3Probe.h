#pragma once

#include <string>

namespace tc002 {

// Where an MP3's audio starts and, when the shared decoder cannot play it, why (issue #12).
struct Mp3Probe {
  long audioOffset = 0;  // first byte after a leading ID3v2 tag
  std::string problem;   // empty when the first frames are MPEG-1 Layer III
};

// The decoder gives up after 128 KB without a frame, so large cover art has to be skipped by the
// caller, and it only plays MPEG-1 Layer III, so other files are named here instead of failing
// with a bare "decoding failed".
Mp3Probe probeMp3(const std::string& path);

}

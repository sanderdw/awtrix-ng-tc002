#include "Tc002Mp3Probe.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

static std::string fixtures;

void check(bool ok, const char* what, const std::string& detail) {
  if (!ok) { std::fprintf(stderr, "%s: %s\n", what, detail.c_str()); std::exit(1); }
}
std::string read(const std::string& name) {
  std::ifstream in(fixtures + "/" + name, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}
std::string temp(const std::string& bytes) {
  char path[] = "/tmp/tc002-mp3-XXXXXX";
  const int fd = mkstemp(path);
  check(fd >= 0 && write(fd, bytes.data(), bytes.size()) == ssize_t(bytes.size()), "temp file", path);
  close(fd);
  return path;
}
// An ID3v2.3 tag of the given body size, as cover art would make it.
std::string id3(long body) {
  std::string tag = "ID3";
  tag += {3, 0, 0, char((body >> 21) & 0x7F), char((body >> 14) & 0x7F), char((body >> 7) & 0x7F),
          char(body & 0x7F)};
  return tag + std::string(body, '\xFF');
}
tc002::Mp3Probe probe(const std::string& bytes) {
  const std::string path = temp(bytes);
  const auto result = tc002::probeMp3(path);
  unlink(path.c_str());
  return result;
}

int main(int argc, char** argv) {
  check(argc == 2, "usage", "mp3_probe_test <fixtures dir>");
  fixtures = argv[1];
  const std::string mpeg1 = read("mp3-mpeg1.mp3");
  check(mpeg1.size() > 100, "fixture", "mp3-mpeg1.mp3");

  auto r = probe(mpeg1);
  check(r.problem.empty(), "MPEG-1 refused", r.problem);
  check(r.audioOffset == 20, "MPEG-1 offset", std::to_string(r.audioOffset));

  // Cover art past the decoder's 128 KB scan: the audio starts after the tag.
  const std::string art = id3(200 * 1024);
  r = probe(art + mpeg1.substr(20));
  check(r.problem.empty(), "cover art refused", r.problem);
  check(r.audioOffset == long(art.size()), "cover art offset", std::to_string(r.audioOffset));

  r = probe(read("mp3-mpeg2.mp3"));
  check(r.problem.find("MPEG-2 audio at 22050 Hz") == 0, "MPEG-2", r.problem);
  r = probe(read("mp3-layer2.mp3"));
  check(r.problem.find("Layer I/II") != std::string::npos, "MP2", r.problem);
  r = probe("ID3" + std::string(20, '\0'));
  check(r.problem == "no MP3 audio frames found", "empty tag", r.problem);
  r = probe(std::string(4096, '\xFF'));
  check(r.problem == "no MP3 audio frames found", "sync noise", r.problem);
  r = probe("");
  check(r.problem == "no MP3 audio frames found", "empty file", r.problem);
  check(tc002::probeMp3("/nonexistent/x.mp3").problem.find("could not be opened") != std::string::npos,
        "missing file", "");
}

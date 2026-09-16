#pragma once
#include <cstdint>
#include <string>
namespace tc002 {
struct FirmwareImage { uint32_t size=0; unsigned char first[16]{}; };
bool validateFirmware(int fd,FirmwareImage& image,std::string& error);
}

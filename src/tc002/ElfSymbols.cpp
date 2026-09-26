#include "ElfSymbols.h"

#include <elf.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>

namespace tc002 {

namespace {

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "ELF structures are copied as stored");

// Far larger than any vendor library on the clock, and it keeps every offset below 2^31 for the
// clock's 32-bit off_t.
constexpr uint64_t kMaxFile = 64u << 20;

bool readAt(int fd, void* out, uint64_t size, uint64_t offset) {
  char* bytes = static_cast<char*>(out);
  while (size > 0) {
    const ssize_t n = pread(fd, bytes, static_cast<size_t>(size), static_cast<off_t>(offset));
    if (n <= 0) return false;
    bytes += n;
    size -= static_cast<uint64_t>(n);
    offset += static_cast<uint64_t>(n);
  }
  return true;
}

bool inside(uint64_t offset, uint64_t length, uint64_t fileSize) {
  return offset <= fileSize && length <= fileSize - offset;
}

template <typename Ehdr, typename Shdr, typename Sym>
bool scan(int fd, uint64_t fileSize, const std::vector<std::string>& wanted, std::vector<bool>& found) {
  Ehdr header;
  if (fileSize < sizeof(header) || !readAt(fd, &header, sizeof(header), 0)) return false;
  if (header.e_type != ET_DYN || header.e_shentsize != sizeof(Shdr) || header.e_shnum == 0) return false;
  const uint64_t tableSize = uint64_t(header.e_shnum) * sizeof(Shdr);
  if (!inside(header.e_shoff, tableSize, fileSize)) return false;
  std::vector<Shdr> sections(header.e_shnum);
  if (!readAt(fd, sections.data(), tableSize, header.e_shoff)) return false;
  const Shdr* symbols = nullptr;
  for (const Shdr& section : sections)
    if (section.sh_type == SHT_DYNSYM) { symbols = &section; break; }
  if (!symbols || symbols->sh_entsize != sizeof(Sym) || symbols->sh_size % sizeof(Sym) != 0 ||
      !inside(symbols->sh_offset, symbols->sh_size, fileSize) || symbols->sh_link >= header.e_shnum)
    return false;
  const Shdr& names = sections[symbols->sh_link];
  if (names.sh_type != SHT_STRTAB || !inside(names.sh_offset, names.sh_size, fileSize)) return false;
  std::vector<char> table(symbols->sh_size), strings(names.sh_size);
  if (!readAt(fd, table.data(), table.size(), symbols->sh_offset) ||
      !readAt(fd, strings.data(), strings.size(), names.sh_offset))
    return false;
  // Entry 0 is the reserved null symbol. A name outside the string table makes the whole table
  // untrustworthy, so the file is rejected rather than read around.
  for (size_t offset = sizeof(Sym); offset < table.size(); offset += sizeof(Sym)) {
    Sym symbol;
    std::memcpy(&symbol, table.data() + offset, sizeof(symbol));
    if (symbol.st_name >= strings.size() ||
        !std::memchr(strings.data() + symbol.st_name, 0, strings.size() - symbol.st_name))
      return false;
    const unsigned bind = symbol.st_info >> 4, type = symbol.st_info & 0xf;
    if ((bind != STB_GLOBAL && bind != STB_WEAK) || type != STT_FUNC || symbol.st_shndx == SHN_UNDEF) continue;
    const char* name = strings.data() + symbol.st_name;
    for (size_t i = 0; i < wanted.size(); ++i)
      if (!found[i] && wanted[i] == name) found[i] = true;
  }
  return true;
}

}

bool elfMissingFunctions(const std::string& path, const std::vector<std::string>& wanted,
                         std::vector<std::string>& missing) {
  missing.clear();
  // O_NONBLOCK: opening a FIFO planted at the path must not hang the updater.
  const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
  if (fd < 0) return false;
  struct stat info{};
  unsigned char ident[EI_NIDENT];
  std::vector<bool> found(wanted.size(), false);
  bool ok = fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && uint64_t(info.st_size) <= kMaxFile &&
            uint64_t(info.st_size) >= EI_NIDENT && readAt(fd, ident, EI_NIDENT, 0) &&
            !std::memcmp(ident, ELFMAG, SELFMAG) && ident[EI_DATA] == ELFDATA2LSB;
  if (ok && ident[EI_CLASS] == ELFCLASS32)
    ok = scan<Elf32_Ehdr, Elf32_Shdr, Elf32_Sym>(fd, uint64_t(info.st_size), wanted, found);
  else if (ok && ident[EI_CLASS] == ELFCLASS64)
    ok = scan<Elf64_Ehdr, Elf64_Shdr, Elf64_Sym>(fd, uint64_t(info.st_size), wanted, found);
  else
    ok = false;
  close(fd);
  if (!ok) return false;
  for (size_t i = 0; i < wanted.size(); ++i)
    if (!found[i]) missing.push_back(wanted[i]);
  return true;
}

}

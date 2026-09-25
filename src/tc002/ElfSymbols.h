#pragma once
// Reads which functions a vendor shared library defines, without loading it: dlopen would run the
// library's constructors, and the point of the check is to decide whether that is safe to rely on.
#include <string>
#include <vector>

namespace tc002 {

// Looks up `wanted` in the dynamic symbol table (.dynsym, found through the section headers) of a
// little-endian ELF32 or ELF64 shared object. Returns false when the file is not such an object or
// any table in it lies outside the file; otherwise `missing` lists, in the order asked, the names it
// does not define as GLOBAL or WEAK functions. tools/install.py applies the same rules in Python.
bool elfMissingFunctions(const std::string& path, const std::vector<std::string>& wanted,
                         std::vector<std::string>& missing);

}

#ifndef PLATFORM_FILE_IO_HPP
#define PLATFORM_FILE_IO_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace platform
{

// Shared file reader — was duplicated in AtlasPack and TilemapRuntime.
// Uses fstat to pre-allocate the vector, reducing heap fragmentation on 32MB PS2.
bool ReadWholeFile(const std::string &path,
                   std::vector<uint8_t> *outBytes,
                   std::string *outError);

} // namespace platform

#endif

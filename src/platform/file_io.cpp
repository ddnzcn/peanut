#include "platform/file_io.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace platform
{

bool ReadWholeFile(const std::string &path,
                   std::vector<uint8_t> *outBytes,
                   std::string *outError)
{
  outBytes->clear();

  const int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0)
  {
    *outError = "Failed to open file: " + path;
    return false;
  }

  struct stat st;
  if (fstat(fd, &st) == 0 && st.st_size > 0)
  {
    outBytes->reserve(static_cast<size_t>(st.st_size));
  }

  uint8_t chunk[4096];
  for (;;)
  {
    const int bytesRead = read(fd, chunk, sizeof(chunk));
    if (bytesRead < 0)
    {
      close(fd);
      outBytes->clear();
      *outError = "Failed to read file: " + path;
      return false;
    }

    if (bytesRead == 0)
    {
      break;
    }

    outBytes->insert(outBytes->end(), chunk, chunk + bytesRead);
  }

  close(fd);
  return true;
}

} // namespace platform

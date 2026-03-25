#include "platform/asset_path.hpp"

#include <cctype>

#ifndef PS2_ASSET_DEVICE
#define PS2_ASSET_DEVICE "host"
#endif

#ifndef PS2_ASSET_ROOT
#define PS2_ASSET_ROOT ""
#endif

namespace platform
{

namespace
{

bool HasDevicePrefix(const std::string &path)
{
  return path.find(':') != std::string::npos;
}

std::string TrimLeadingSeparators(const std::string &path)
{
  size_t index = 0;
  while (index < path.size() &&
         (path[index] == '/' || path[index] == '\\'))
  {
    ++index;
  }
  return path.substr(index);
}

std::string TrimTrailingSeparators(const std::string &path)
{
  size_t end = path.size();
  while (end > 0 &&
         (path[end - 1] == '/' || path[end - 1] == '\\'))
  {
    --end;
  }
  return path.substr(0, end);
}

std::string ReplaceSeparators(const std::string &path, char separator)
{
  std::string result = path;
  for (size_t i = 0; i < result.size(); ++i)
  {
    if (result[i] == '/' || result[i] == '\\')
    {
      result[i] = separator;
    }
  }
  return result;
}

std::string JoinNormalized(const std::string &root,
                           const std::string &relativePath,
                           char separator)
{
  const std::string cleanRoot =
      TrimTrailingSeparators(ReplaceSeparators(root, separator));
  const std::string cleanRelative =
      TrimLeadingSeparators(ReplaceSeparators(relativePath, separator));

  if (cleanRoot.empty())
  {
    return cleanRelative;
  }

  if (cleanRelative.empty())
  {
    return cleanRoot;
  }

  return cleanRoot + separator + cleanRelative;
}

std::string ToLower(std::string value)
{
  for (size_t i = 0; i < value.size(); ++i)
  {
    value[i] = static_cast<char>(
        std::tolower(static_cast<unsigned char>(value[i])));
  }
  return value;
}

} // namespace

std::string ResolveAssetPath(const std::string &relativePath)
{
  if (relativePath.empty() || HasDevicePrefix(relativePath))
  {
    return relativePath;
  }

  const std::string device = ToLower(PS2_ASSET_DEVICE);
  const std::string root = PS2_ASSET_ROOT;

  if (device == "cdrom0")
  {
    std::string path = JoinNormalized(root, relativePath, '\\');
    if (path.empty())
    {
      return "cdrom0:\\";
    }
    if (path.find(';') == std::string::npos)
    {
      path += ";1";
    }
    return "cdrom0:\\" + path;
  }

  if (device == "mass")
  {
    const std::string path = JoinNormalized(root, relativePath, '/');
    if (path.empty())
    {
      return "mass:/";
    }
    return "mass:/" + path;
  }

  if (device == "host")
  {
    const std::string path = JoinNormalized(root, relativePath, '/');
    return "host:" + path;
  }

  const std::string path = JoinNormalized(root, relativePath, '/');
  if (path.empty())
  {
    return device + ":";
  }
  return device + ":" + path;
}

} // namespace platform

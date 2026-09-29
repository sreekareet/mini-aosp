// Where things live on disk. All binaries are in <build>/bin and the HAL plugin
// is in <build>/lib, so paths are derived from the running executable.
#pragma once

#include <limits.h>
#include <unistd.h>

#include <string>

namespace mini {

// Directory containing the running executable, e.g. /home/me/mini-aosp/build/bin
inline std::string exeDir() {
  char buf[PATH_MAX];
  ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0) return ".";
  std::string path(buf, static_cast<size_t>(n));
  return path.substr(0, path.find_last_of('/'));
}

inline std::string defaultHalPath() { return exeDir() + "/../lib/libfake_vehicle_hal.so"; }

}  // namespace mini

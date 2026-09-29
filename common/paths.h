// Where things live on disk. All binaries are in <build>/bin and the HAL plugin
// is in <build>/lib, so paths are derived from the running executable.
#pragma once

#include <limits.h>
#include <unistd.h>

#include <cstdlib>
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

// Runtime directory holding every Unix socket (like /dev/socket on Android).
// MINI_AOSP_RUN_DIR overrides it, which is how tests get an isolated sandbox.
inline std::string runDir() {
  const char* env = std::getenv("MINI_AOSP_RUN_DIR");
  if (env != nullptr && *env != '\0') return env;
  return "/tmp/mini-aosp-" + std::to_string(::getuid());
}

inline std::string serviceSocketPath(const std::string& name) {
  return runDir() + "/" + name + ".sock";
}

// The one well-known address, like Binder's context manager (handle 0).
inline std::string serviceManagerSocketPath() { return serviceSocketPath("servicemanager"); }

inline std::string defaultHalPath() { return exeDir() + "/../lib/libfake_vehicle_hal.so"; }

}  // namespace mini

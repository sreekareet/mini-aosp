// Parser for init.conf, a much smaller cousin of Android's init.rc.
//
//   # comment
//   setprop <key> <value...>            set a property at boot
//   service <name> <path> [args...]     declare a service; the lines below refine it:
//       oneshot                           do not restart when it exits
//       restart_delay <milliseconds>      wait before restarting (default 200)
//
// A path without a '/' is looked up next to the init executable.
#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/result.h"

namespace mini {

struct ServiceDef {
  std::string name;
  std::string path;
  std::vector<std::string> args;
  bool oneshot = false;
  std::chrono::milliseconds restartDelay{200};
};

struct InitConfig {
  std::vector<std::pair<std::string, std::string>> properties;  // in file order
  std::vector<ServiceDef> services;                             // in file order = start order
};

// On failure returns InvalidArgument and fills `error` with "line N: what is wrong".
Result<InitConfig> parseInitConfig(std::string_view text, std::string& error);

}  // namespace mini

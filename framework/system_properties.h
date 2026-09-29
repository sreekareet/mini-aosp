// SystemProperties: app-facing access to the system property store.
// Real AOSP: android.os.SystemProperties / getprop / setprop.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "common/result.h"

namespace mini {

// Each call connects to init's property service, asks, and disconnects: simple, and
// fine for something called rarely. (AOSP maps the property area into every process
// so reads never need IPC - noted in the study material.)
class SystemProperties {
 public:
  static Result<std::string> get(const std::string& key);  // NotFound if unset
  static std::string get(const std::string& key, const std::string& fallback);
  static Status set(const std::string& key, const std::string& value);
  static Result<std::vector<std::pair<std::string, std::string>>> list();
};

}  // namespace mini

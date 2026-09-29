// The system property store: a flat map of string keys to string values.
// Real AOSP: system properties (getprop/setprop), served by init's property service.
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/result.h"

namespace mini {

// Keys look like "ro.build.type" or "init.svc.vehicle_service". Rules (simplified
// from Android): keys use [A-Za-z0-9._-], values are bounded, and keys starting
// with "ro." can be set once and never changed ("read-only" after first write).
//
// Plain data structure, no locking: the property service owns it and only touches
// it from the init loop thread.
class PropertyStore {
 public:
  static constexpr size_t kMaxKeyLength = 64;
  static constexpr size_t kMaxValueLength = 92;  // the same limit as Android's PROP_VALUE_MAX

  static bool isValidKey(std::string_view key);

  Result<std::string> get(const std::string& key) const;  // NotFound if unset
  // InvalidArgument: bad key or value too long. ReadOnly: "ro." key that is already set.
  Status set(const std::string& key, const std::string& value);
  std::vector<std::pair<std::string, std::string>> list() const;  // sorted by key

 private:
  std::map<std::string, std::string> props_;
};

}  // namespace mini

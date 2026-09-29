#include "init/property_store.h"

namespace mini {

bool PropertyStore::isValidKey(std::string_view key) {
  if (key.empty() || key.size() > kMaxKeyLength || key.front() == '.' || key.back() == '.') {
    return false;
  }
  for (char c : key) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

Result<std::string> PropertyStore::get(const std::string& key) const {
  auto it = props_.find(key);
  if (it == props_.end()) return Status::NotFound;
  return it->second;
}

Status PropertyStore::set(const std::string& key, const std::string& value) {
  if (!isValidKey(key) || value.size() > kMaxValueLength) return Status::InvalidArgument;
  auto it = props_.find(key);
  if (it != props_.end() && key.rfind("ro.", 0) == 0) return Status::ReadOnly;
  props_[key] = value;
  return Status::Ok;
}

std::vector<std::pair<std::string, std::string>> PropertyStore::list() const {
  return {props_.begin(), props_.end()};
}

}  // namespace mini

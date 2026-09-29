#include "framework/system_properties.h"

#include "common/paths.h"
#include "init/property_service.h"

namespace mini {

namespace {
Result<PropertyServiceProxy> connect() {
  auto remote = SocketRemote::connect(serviceSocketPath(kPropertyServiceName));
  if (!remote) return remote.status();
  return PropertyServiceProxy(remote.take());
}
}  // namespace

Result<std::string> SystemProperties::get(const std::string& key) {
  auto proxy = connect();
  if (!proxy) return proxy.status();
  return proxy.value().get(key);
}

std::string SystemProperties::get(const std::string& key, const std::string& fallback) {
  return get(key).valueOr(fallback);
}

Status SystemProperties::set(const std::string& key, const std::string& value) {
  auto proxy = connect();
  if (!proxy) return proxy.status();
  return proxy.value().set(key, value);
}

Result<std::vector<std::pair<std::string, std::string>>> SystemProperties::list() {
  auto proxy = connect();
  if (!proxy) return proxy.status();
  return proxy.value().list();
}

}  // namespace mini

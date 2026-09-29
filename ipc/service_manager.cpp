#include "ipc/service_manager.h"

#include <thread>

#include "common/paths.h"

namespace mini {

namespace {
bool validName(const std::string& name) {
  if (name.empty() || name.size() > 64) return false;
  for (char c : name) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}
}  // namespace

Status ServiceManager::onTransact(uint32_t code, Parcel& data, Parcel& reply,
                                  const CallerInfo& /*caller*/) {
  switch (code) {
    case kAddService: {
      std::string name = data.readString();
      std::string path = data.readString();
      if (!data.ok()) return Status::BadParcel;
      if (!validName(name) || path.empty()) return Status::InvalidArgument;
      services_[name] = path;  // re-registering replaces: a restarted service takes over its name
      return Status::Ok;
    }
    case kGetService: {
      std::string name = data.readString();
      if (!data.ok()) return Status::BadParcel;
      auto it = services_.find(name);
      if (it == services_.end()) return Status::NotFound;
      reply.writeString(it->second);
      return Status::Ok;
    }
    case kListServices: {
      reply.writeInt32(static_cast<int32_t>(services_.size()));
      for (const auto& [name, path] : services_) reply.writeString(name);
      return Status::Ok;
    }
  }
  return Status::UnknownTransaction;
}

Status ServiceManagerProxy::addService(const std::string& name, const std::string& socketPath) {
  Parcel data;
  data.writeString(name);
  data.writeString(socketPath);
  return remote_->transact(kAddService, data).status();
}

Result<std::string> ServiceManagerProxy::getService(const std::string& name) {
  Parcel data;
  data.writeString(name);
  Result<Parcel> r = remote_->transact(kGetService, data);
  if (!r) return r.status();
  std::string path = r.value().readString();
  if (!r.value().ok()) return Status::BadParcel;
  return path;
}

Result<std::vector<std::string>> ServiceManagerProxy::listServices() {
  Result<Parcel> r = remote_->transact(kListServices, Parcel());
  if (!r) return r.status();
  Parcel& p = r.value();
  int32_t n = p.readInt32();
  std::vector<std::string> names;
  for (int32_t i = 0; i < n && p.ok(); ++i) names.push_back(p.readString());
  if (!p.ok()) return Status::BadParcel;
  return names;
}

Result<std::unique_ptr<SocketRemote>> connectToServiceManager(std::chrono::milliseconds wait) {
  auto deadline = std::chrono::steady_clock::now() + wait;
  for (;;) {
    auto r = SocketRemote::connect(serviceManagerSocketPath());
    if (r || std::chrono::steady_clock::now() >= deadline) return r;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

Status registerService(const std::string& name, const std::string& socketPath,
                       std::chrono::milliseconds wait) {
  auto remote = connectToServiceManager(wait);
  if (!remote) return remote.status();
  return ServiceManagerProxy(remote.take()).addService(name, socketPath);
}

Result<std::unique_ptr<SocketRemote>> getServiceRemote(const std::string& name) {
  auto sm = connectToServiceManager();
  if (!sm) return sm.status();
  Result<std::string> path = ServiceManagerProxy(sm.take()).getService(name);
  if (!path) return path.status();
  return SocketRemote::connect(path.value());
}

}  // namespace mini

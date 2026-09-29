#include "init/property_service.h"

#include "common/log.h"

namespace mini {

namespace {
bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }
}  // namespace

bool canSetProperty(const std::string& key, uid_t callerUid, uid_t ownerUid) {
  if (startsWith(key, "ro.") || startsWith(key, "init.svc.") || startsWith(key, "init.svc_debug_pid.")) {
    return false;
  }
  if (startsWith(key, "sys.") || startsWith(key, "debug.")) return true;
  return callerUid == 0 || callerUid == ownerUid;
}

Status PropertyServiceStub::onTransact(uint32_t code, Parcel& data, Parcel& reply,
                                       const CallerInfo& caller) {
  switch (code) {
    case kGetProp: {
      std::string key = data.readString();
      if (!data.ok()) return Status::BadParcel;
      Result<std::string> v = store_.get(key);
      if (!v) return v.status();
      reply.writeString(v.value());
      return Status::Ok;
    }
    case kSetProp: {
      std::string key = data.readString();
      std::string value = data.readString();
      if (!data.ok()) return Status::BadParcel;
      if (!canSetProperty(key, caller.uid, ownerUid_)) {
        logWarn("property_service", "uid %d (pid %d) may not set '%s'", static_cast<int>(caller.uid),
                static_cast<int>(caller.pid), key.c_str());
        return Status::PermissionDenied;
      }
      return store_.set(key, value);
    }
    case kListProps: {
      auto all = store_.list();
      reply.writeInt32(static_cast<int32_t>(all.size()));
      for (const auto& [k, v] : all) {
        reply.writeString(k);
        reply.writeString(v);
      }
      return Status::Ok;
    }
  }
  return Status::UnknownTransaction;
}

Result<std::string> PropertyServiceProxy::get(const std::string& key) {
  Parcel data;
  data.writeString(key);
  Result<Parcel> r = remote_->transact(kGetProp, data);
  if (!r) return r.status();
  std::string v = r.value().readString();
  if (!r.value().ok()) return Status::BadParcel;
  return v;
}

Status PropertyServiceProxy::set(const std::string& key, const std::string& value) {
  Parcel data;
  data.writeString(key);
  data.writeString(value);
  return remote_->transact(kSetProp, data).status();
}

Result<std::vector<std::pair<std::string, std::string>>> PropertyServiceProxy::list() {
  Result<Parcel> r = remote_->transact(kListProps, Parcel());
  if (!r) return r.status();
  Parcel& p = r.value();
  int32_t n = p.readInt32();
  std::vector<std::pair<std::string, std::string>> out;
  for (int32_t i = 0; i < n && p.ok(); ++i) {
    std::string k = p.readString();
    std::string v = p.readString();
    out.emplace_back(std::move(k), std::move(v));
  }
  if (!p.ok()) return Status::BadParcel;
  return out;
}

}  // namespace mini

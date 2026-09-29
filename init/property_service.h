// The property service: exposes a PropertyStore over IPC. It runs inside init, as
// in AOSP, and listens on a well-known socket (like /dev/socket/property_service).
// Same proxy/stub pattern as the vehicle service.
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "init/property_store.h"
#include "ipc/remote.h"
#include "ipc/stub.h"

namespace mini {

inline constexpr const char* kPropertyServiceName = "property_service";

enum PropertyTx : uint32_t {
  kGetProp = 1,    // (string key) -> (string value)
  kSetProp = 2,    // (string key, string value) -> ()
  kListProps = 3,  // () -> (int32 n, [string key, string value] * n)
};

// May a client with this uid set this property through the service?
//   ro.*, init.svc.*   never - only init itself writes them (directly, not through IPC)
//   sys.*, debug.*     anyone (the "world-writable" namespaces)
//   everything else    root or `ownerUid` (the user running init) only
// Real AOSP decides with SELinux property contexts; this keeps the same shape.
bool canSetProperty(const std::string& key, uid_t callerUid, uid_t ownerUid);

// Server side ("Bn"): unmarshals, checks permission, calls the store.
class PropertyServiceStub final : public IStub {
 public:
  PropertyServiceStub(PropertyStore& store, uid_t ownerUid) : store_(store), ownerUid_(ownerUid) {}
  Status onTransact(uint32_t code, Parcel& data, Parcel& reply, const CallerInfo& caller) override;

 private:
  PropertyStore& store_;
  uid_t ownerUid_;
};

// Client side ("Bp").
class PropertyServiceProxy {
 public:
  explicit PropertyServiceProxy(std::unique_ptr<IRemote> remote) : remote_(std::move(remote)) {}

  Result<std::string> get(const std::string& key);
  Status set(const std::string& key, const std::string& value);
  Result<std::vector<std::pair<std::string, std::string>>> list();

 private:
  std::unique_ptr<IRemote> remote_;
};

}  // namespace mini

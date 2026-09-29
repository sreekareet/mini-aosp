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

// Server side ("Bn"): unmarshals and calls the store.
class PropertyServiceStub final : public IStub {
 public:
  explicit PropertyServiceStub(PropertyStore& store) : store_(store) {}
  Status onTransact(uint32_t code, Parcel& data, Parcel& reply, const CallerInfo& caller) override;

 private:
  PropertyStore& store_;
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

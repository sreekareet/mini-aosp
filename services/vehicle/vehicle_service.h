// Server side of the vehicle service: real implementation + the stub that feeds it.
#pragma once

#include <map>
#include <set>
#include <vector>

#include "ipc/access_policy.h"
#include "ipc/stub.h"
#include "services/vehicle/vehicle_iface.h"

namespace mini {

// The real implementation, backed by the HAL. Knows nothing about IPC, so it can
// be tested and reused directly. Real AOSP: the vehicle service inside CarService.
//
// Not thread-safe by design: the HAL, the service and the stub are all used only
// from the server's EventLoop thread (HAL callbacks included, since poll() runs
// there), so no locks are needed.
class VehicleService final : public IVehicleService {
 public:
  explicit VehicleService(IVehicleHal& hal);  // hal must outlive the service
  ~VehicleService() override;

  Result<PropValue> getProperty(PropertyId id) override { return hal_.get(id); }
  Status setProperty(PropertyId id, const PropValue& value) override {
    return hal_.set(id, value);
  }
  Result<std::vector<PropertyConfig>> listProperties() override { return hal_.listProperties(); }
  Status subscribe(PropertyId id, std::shared_ptr<IPropertyListener> listener) override;
  Status unsubscribe(PropertyId id, const std::shared_ptr<IPropertyListener>& listener) override;

 private:
  void onHalChange(PropertyId id, const PropValue& value);

  IVehicleHal& hal_;
  std::map<PropertyId, std::vector<std::shared_ptr<IPropertyListener>>> listeners_;
};

// Unmarshals a transaction, calls the implementation, marshals the answer.
// Real AOSP: the generated BnVehicleService.
class VehicleStub final : public IStub {
 public:
  // Anyone may read and subscribe; only `writers` may set properties. Real AOSP checks a
  // per-property permission (e.g. CONTROL_CAR_CLIMATE) against the caller's uid.
  explicit VehicleStub(IVehicleService& impl, UidAllowList writers = UidAllowList::anyone())
      : impl_(impl), writers_(std::move(writers)) {}

  Status onTransact(uint32_t code, Parcel& data, Parcel& reply, const CallerInfo& caller) override;
  void onClientGone(ConnectionId connection) override;

 private:
  class RemoteListener;  // forwards property changes to one client as Event frames

  struct ClientState {
    std::shared_ptr<RemoteListener> listener;
    std::set<PropertyId> subscribed;
  };

  IVehicleService& impl_;
  UidAllowList writers_;
  std::map<ConnectionId, ClientState> clients_;
};

}  // namespace mini

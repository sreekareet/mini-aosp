// Server side of the vehicle service: real implementation + the stub that feeds it.
#pragma once

#include "ipc/stub.h"
#include "services/vehicle/vehicle_iface.h"

namespace mini {

// The real implementation, backed by the HAL. Knows nothing about IPC, so it can
// be tested and reused directly. Real AOSP: the vehicle service inside CarService.
class VehicleService final : public IVehicleService {
 public:
  explicit VehicleService(IVehicleHal& hal) : hal_(hal) {}  // hal must outlive the service

  Result<PropValue> getProperty(PropertyId id) override { return hal_.get(id); }
  Status setProperty(PropertyId id, const PropValue& value) override {
    return hal_.set(id, value);
  }
  Result<std::vector<PropertyConfig>> listProperties() override { return hal_.listProperties(); }

 private:
  IVehicleHal& hal_;
};

// Unmarshals a transaction, calls the implementation, marshals the answer.
// Real AOSP: the generated BnVehicleService.
class VehicleStub final : public IStub {
 public:
  explicit VehicleStub(IVehicleService& impl) : impl_(impl) {}

  Status onTransact(uint32_t code, Parcel& data, Parcel& reply, const CallerInfo& caller) override;

 private:
  IVehicleService& impl_;
};

}  // namespace mini

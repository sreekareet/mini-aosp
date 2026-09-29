// The vehicle service's interface, shared by client and server - the role an AIDL
// file plays in AOSP. Three pieces:
//   IVehicleService        the C++ interface (what callers use)
//   VehicleServiceProxy    "Bp": implements it by sending transactions
//   VehicleStub (in vehicle_service.h): "Bn": receives transactions, calls a real impl
// Callers can't tell a proxy from a local implementation - that is the point.
#pragma once

#include <memory>
#include <vector>

#include "hal/vehicle_hal.h"
#include "ipc/remote.h"

namespace mini {

inline constexpr const char* kVehicleServiceName = "vehicle";

enum VehicleTx : uint32_t {
  kGetProperty = 1,    // (int32 id) -> (value)
  kSetProperty = 2,    // (int32 id, value) -> ()
  kListProperties = 3, // () -> (int32 n, [int32 id, int32 type, bool writable] * n)
};

class IVehicleService {
 public:
  virtual ~IVehicleService() = default;
  virtual Result<PropValue> getProperty(PropertyId id) = 0;
  virtual Status setProperty(PropertyId id, const PropValue& value) = 0;
  virtual Result<std::vector<PropertyConfig>> listProperties() = 0;
};

// Wire encoding of a PropValue: [int32 ValueType][payload]. Decoding a bad tag
// marks the Parcel bad.
void writeValue(Parcel& p, const PropValue& v);
PropValue readValue(Parcel& p);

class VehicleServiceProxy final : public IVehicleService {
 public:
  explicit VehicleServiceProxy(std::unique_ptr<IRemote> remote) : remote_(std::move(remote)) {}

  Result<PropValue> getProperty(PropertyId id) override;
  Status setProperty(PropertyId id, const PropValue& value) override;
  Result<std::vector<PropertyConfig>> listProperties() override;

 private:
  std::unique_ptr<IRemote> remote_;
};

}  // namespace mini

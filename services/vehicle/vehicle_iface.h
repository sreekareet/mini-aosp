// The vehicle service's interface, shared by client and server - the role an AIDL
// file plays in AOSP. Three pieces:
//   IVehicleService        the C++ interface (what callers use)
//   VehicleServiceProxy    "Bp": implements it by sending transactions
//   VehicleStub (in vehicle_service.h): "Bn": receives transactions, calls a real impl
// Callers can't tell a proxy from a local implementation - that is the point.
#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include "hal/vehicle_hal.h"
#include "ipc/remote.h"

namespace mini {

inline constexpr const char* kVehicleServiceName = "vehicle";

enum VehicleTx : uint32_t {
  kGetProperty = 1,     // (int32 id) -> (value)
  kSetProperty = 2,     // (int32 id, value) -> ()
  kListProperties = 3,  // () -> (int32 n, [int32 id, int32 type, bool writable] * n)
  kSubscribe = 4,       // (int32 id) -> ()      updates then arrive as kOnPropertyChanged events
  kUnsubscribe = 5,     // (int32 id) -> ()
};

// Server -> client event codes (a separate numbering from the calls above).
enum VehicleEvent : uint32_t {
  kOnPropertyChanged = 1,  // (int32 id, value)
};

// The callback interface: implemented by the *client*, called by the service.
// Real AOSP: a callback binder (e.g. ICarPropertyEventListener) passed in subscribe().
class IPropertyListener {
 public:
  virtual ~IPropertyListener() = default;
  virtual void onPropertyChanged(PropertyId id, const PropValue& value) = 0;
};

class IVehicleService {
 public:
  virtual ~IVehicleService() = default;
  virtual Result<PropValue> getProperty(PropertyId id) = 0;
  virtual Status setProperty(PropertyId id, const PropValue& value) = 0;
  virtual Result<std::vector<PropertyConfig>> listProperties() = 0;

  // The listener is called whenever `id` changes. shared_ptr because the caller and
  // the service both hold it (the stand-in for AOSP's sp<>).
  virtual Status subscribe(PropertyId id, std::shared_ptr<IPropertyListener> listener) = 0;
  virtual Status unsubscribe(PropertyId id, const std::shared_ptr<IPropertyListener>& listener) = 0;
};

// Wire encoding of a PropValue: [int32 ValueType][payload]. Decoding a bad tag
// marks the Parcel bad.
void writeValue(Parcel& p, const PropValue& v);
PropValue readValue(Parcel& p);

// Thread-safety: getProperty/setProperty/listProperties may be called from any thread.
// Listener callbacks run on the connection's loop thread.
class VehicleServiceProxy final : public IVehicleService {
 public:
  explicit VehicleServiceProxy(std::unique_ptr<IRemote> remote);

  Result<PropValue> getProperty(PropertyId id) override;
  Status setProperty(PropertyId id, const PropValue& value) override;
  Result<std::vector<PropertyConfig>> listProperties() override;
  Status subscribe(PropertyId id, std::shared_ptr<IPropertyListener> listener) override;
  Status unsubscribe(PropertyId id, const std::shared_ptr<IPropertyListener>& listener) override;

 private:
  struct ListenerTable;  // client-side fan-out, shared with the event handler

  std::unique_ptr<IRemote> remote_;
  std::shared_ptr<ListenerTable> table_;
  std::mutex subscribeMutex_;  // makes "add listener + tell the server" atomic
  bool handlerInstalled_ = false;
};

}  // namespace mini

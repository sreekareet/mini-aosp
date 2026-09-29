// PropertyManager: the app-facing API to vehicle properties. Apps never see sockets,
// parcels or proxies - they see typed keys, get/set and subscribe with a callback.
// Real AOSP: CarPropertyManager (obtained from the Car API).
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "common/event_loop.h"
#include "hal/vehicle_hal.h"
#include "services/vehicle/vehicle_iface.h"

namespace mini {

// A type-safe property key: the value type is part of the key's type, so
// get(kSpeed) returns float and set(kGear, 1.5f) does not compile.
// (Real AOSP uses untyped property ids plus casts; this is the small-template answer.)
template <typename T>
struct PropertyKey {
  PropertyId id;
};

inline constexpr PropertyKey<float> kSpeed{PropertyId::Speed};
inline constexpr PropertyKey<float> kFuelLevel{PropertyId::FuelLevel};
inline constexpr PropertyKey<int32_t> kGear{PropertyId::Gear};
inline constexpr PropertyKey<float> kHvacTemp{PropertyId::HvacTemp};

using SubscriptionId = uint64_t;

// Threading: get/set/list/subscribe/unsubscribe may be called from any thread.
// Subscription callbacks run on the manager's own event-loop thread; keep them short
// and do not call back into the manager from them (a synchronous call there would
// wait on the thread that is running the callback - it fails with IllegalState).
class PropertyManager {
 public:
  using RawCallback = std::function<void(PropertyId, const PropValue&)>;

  // Looks the service up through the ServiceManager and connects.
  static Result<std::unique_ptr<PropertyManager>> connect();
  ~PropertyManager();
  PropertyManager(const PropertyManager&) = delete;
  PropertyManager& operator=(const PropertyManager&) = delete;

  // ---- typed API ----
  template <typename T>
  Result<T> get(PropertyKey<T> key) {
    Result<PropValue> v = getRaw(key.id);
    if (!v) return v.status();
    if (const T* typed = std::get_if<T>(&v.value())) return *typed;
    return Status::InvalidArgument;  // the HAL's type differs from the key's
  }

  template <typename T>
  Status set(PropertyKey<T> key, T value) {
    return setRaw(key.id, PropValue{value});
  }

  template <typename T>
  Result<SubscriptionId> subscribe(PropertyKey<T> key, std::function<void(T)> callback) {
    return subscribeRaw(key.id, [callback = std::move(callback)](PropertyId, const PropValue& v) {
      if (const T* typed = std::get_if<T>(&v)) callback(*typed);
    });
  }

  Status unsubscribe(SubscriptionId subscription);

  // ---- untyped API (for tools that learn property types at runtime) ----
  Result<PropValue> getRaw(PropertyId id);
  Status setRaw(PropertyId id, const PropValue& value);
  Result<std::vector<PropertyConfig>> list();
  Result<SubscriptionId> subscribeRaw(PropertyId id, RawCallback callback);

 private:
  class CallbackListener;  // adapts a std::function to IPropertyListener

  struct Subscription {
    PropertyId id;
    std::shared_ptr<CallbackListener> listener;
  };

  PropertyManager(std::unique_ptr<LoopThread> loopThread, std::unique_ptr<VehicleServiceProxy> proxy)
      : loopThread_(std::move(loopThread)), proxy_(std::move(proxy)) {}

  // Order matters: the proxy (and its connection) is destroyed before the loop it uses.
  std::unique_ptr<LoopThread> loopThread_;
  std::unique_ptr<VehicleServiceProxy> proxy_;

  std::mutex mutex_;  // guards the two fields below
  std::map<SubscriptionId, Subscription> subscriptions_;
  SubscriptionId nextSubscription_ = 1;
};

}  // namespace mini

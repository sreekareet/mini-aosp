// PropertyManager: the app-facing API to vehicle properties. Apps never see sockets,
// parcels or proxies - they see typed keys, get/set and subscribe with a callback.
// It also hides service restarts: if the vehicle service dies, the manager notices,
// reconnects in the background and re-registers every subscription.
// Real AOSP: CarPropertyManager (obtained from the Car API).
#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
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
// Subscription and connection callbacks must be short. Subscription callbacks run on
// the manager's event-loop thread; do not call back into the manager from them (a
// synchronous call there would wait on the very thread running the callback - it
// fails with IllegalState instead of deadlocking).
//
// While the service is down, get/set/list fail with DeadObject. When it is back,
// calls work again and existing subscriptions resume - but the restarted service has
// lost its state (it starts from the HAL's defaults), which apps must tolerate.
class PropertyManager {
 public:
  using RawCallback = std::function<void(PropertyId, const PropValue&)>;
  // connected == false: the service died. true: reconnected. Runs on the manager's
  // reconnect thread, in order, never concurrently with itself. isConnected() and calls
  // may already succeed slightly before the `true` callback is delivered.
  using ConnectionCallback = std::function<void(bool connected)>;

  // Looks the service up through the ServiceManager and connects (once; the caller
  // decides whether to retry). Afterwards reconnection is automatic.
  static Result<std::unique_ptr<PropertyManager>> connect();
  ~PropertyManager();
  PropertyManager(const PropertyManager&) = delete;
  PropertyManager& operator=(const PropertyManager&) = delete;

  bool isConnected();
  void setConnectionCallback(ConnectionCallback callback);

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

  // One connection attempt; lets a death notice be matched to the connection it belongs to.
  struct Attempt {
    bool died = false;  // guarded by mutex_
  };

  PropertyManager();

  std::shared_ptr<VehicleServiceProxy> currentProxy();
  Status tryConnect();                     // connect + resubscribe + install; worker/connect() only
  void onDeath(const std::shared_ptr<Attempt>& attempt);  // loop thread
  void workerLoop();                       // the reconnect thread

  // Order matters. Destruction runs in reverse: the worker is joined and the loop
  // stopped in ~PropertyManager() before any of this is torn down.
  std::unique_ptr<LoopThread> loopThread_;

  // Serialises subscribe/unsubscribe against reconnecting, so a subscription made
  // during a reconnect can never be lost. Lock order: subscriptionMutex_ before mutex_.
  std::mutex subscriptionMutex_;
  std::map<SubscriptionId, Subscription> subscriptions_;  // guarded by subscriptionMutex_
  SubscriptionId nextSubscription_ = 1;                   // guarded by subscriptionMutex_

  std::mutex mutex_;  // guards everything below
  std::condition_variable cv_;
  std::shared_ptr<VehicleServiceProxy> proxy_;  // null while disconnected
  std::shared_ptr<Attempt> installed_;          // the attempt that produced proxy_
  bool disconnected_ = false;                   // set by onDeath, consumed by the worker
  bool stopping_ = false;
  ConnectionCallback connectionCallback_;
  std::thread worker_;
};

}  // namespace mini

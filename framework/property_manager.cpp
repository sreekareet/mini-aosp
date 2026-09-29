#include "framework/property_manager.h"

#include "ipc/rpc_client.h"
#include "ipc/service_manager.h"

namespace mini {

class PropertyManager::CallbackListener final : public IPropertyListener {
 public:
  explicit CallbackListener(RawCallback cb) : cb_(std::move(cb)) {}
  void onPropertyChanged(PropertyId id, const PropValue& value) override { cb_(id, value); }

 private:
  RawCallback cb_;
};

Result<std::unique_ptr<PropertyManager>> PropertyManager::connect() {
  Result<std::string> path = lookupService(kVehicleServiceName);
  if (!path) return path.status();

  auto loopThread = std::make_unique<LoopThread>();
  Result<std::unique_ptr<RpcClient>> client = RpcClient::connect(loopThread->loop(), path.value());
  if (!client) return client.status();

  auto proxy = std::make_unique<VehicleServiceProxy>(client.take());
  return std::unique_ptr<PropertyManager>(
      new PropertyManager(std::move(loopThread), std::move(proxy)));  // private ctor
}

PropertyManager::~PropertyManager() = default;

Result<PropValue> PropertyManager::getRaw(PropertyId id) { return proxy_->getProperty(id); }

Status PropertyManager::setRaw(PropertyId id, const PropValue& value) {
  return proxy_->setProperty(id, value);
}

Result<std::vector<PropertyConfig>> PropertyManager::list() { return proxy_->listProperties(); }

Result<SubscriptionId> PropertyManager::subscribeRaw(PropertyId id, RawCallback callback) {
  auto listener = std::make_shared<CallbackListener>(std::move(callback));
  Status s = proxy_->subscribe(id, listener);
  if (s != Status::Ok) return s;

  std::lock_guard<std::mutex> lock(mutex_);
  SubscriptionId sid = nextSubscription_++;
  subscriptions_[sid] = Subscription{id, std::move(listener)};
  return sid;
}

Status PropertyManager::unsubscribe(SubscriptionId subscription) {
  Subscription sub;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(subscription);
    if (it == subscriptions_.end()) return Status::NotFound;
    sub = std::move(it->second);
    subscriptions_.erase(it);
  }
  return proxy_->unsubscribe(sub.id, sub.listener);
}

}  // namespace mini

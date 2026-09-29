#include "framework/property_manager.h"

#include <algorithm>

#include "common/log.h"
#include "ipc/rpc_client.h"
#include "ipc/service_manager.h"

namespace mini {

namespace {
constexpr const char* kTag = "PropertyManager";
constexpr std::chrono::milliseconds kFirstRetryDelay(100);
constexpr std::chrono::milliseconds kMaxRetryDelay(2000);
}  // namespace

class PropertyManager::CallbackListener final : public IPropertyListener {
 public:
  explicit CallbackListener(RawCallback cb) : cb_(std::move(cb)) {}
  void onPropertyChanged(PropertyId id, const PropValue& value) override { cb_(id, value); }

 private:
  RawCallback cb_;
};

PropertyManager::PropertyManager() : loopThread_(std::make_unique<LoopThread>()) {}

Result<std::unique_ptr<PropertyManager>> PropertyManager::connect() {
  std::unique_ptr<PropertyManager> pm(new PropertyManager());  // private ctor
  Status s = pm->tryConnect();
  if (s != Status::Ok) return s;
  pm->worker_ = std::thread([raw = pm.get()] { raw->workerLoop(); });
  return pm;
}

PropertyManager::~PropertyManager() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
  // No callback (death notice, subscription) may run once members start dying.
  loopThread_->stop();
}

bool PropertyManager::isConnected() {
  std::lock_guard<std::mutex> lock(mutex_);
  return proxy_ != nullptr;
}

void PropertyManager::setConnectionCallback(ConnectionCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  connectionCallback_ = std::move(callback);
}

std::shared_ptr<VehicleServiceProxy> PropertyManager::currentProxy() {
  std::lock_guard<std::mutex> lock(mutex_);
  return proxy_;  // a copy: a call in flight keeps its proxy alive across a reconnect
}

// ---- connecting and reconnecting ----------------------------------------------

Status PropertyManager::tryConnect() {
  std::lock_guard<std::mutex> subscriptions(subscriptionMutex_);

  Result<std::string> path = lookupService(kVehicleServiceName);
  if (!path) return path.status();
  Result<std::unique_ptr<RpcClient>> client = RpcClient::connect(loopThread_->loop(), path.value());
  if (!client) return client.status();

  // Ask for the death notice *before* using the connection, so a death is never missed.
  auto attempt = std::make_shared<Attempt>();
  Status s = client.value()->linkToDeath([this, attempt] { onDeath(attempt); });
  if (s != Status::Ok) return s;  // died already

  auto proxy = std::make_shared<VehicleServiceProxy>(client.take());
  // Re-register what the app had subscribed to; the app never notices the outage.
  for (const auto& [sid, sub] : subscriptions_) {
    s = proxy->subscribe(sub.id, sub.listener);
    if (s != Status::Ok) return s;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    proxy_ = std::move(proxy);
    installed_ = attempt;
    if (attempt->died) disconnected_ = true;  // it died before we installed it
  }
  cv_.notify_all();
  return Status::Ok;
}

// Runs on the loop thread. Only records the fact; the worker does the slow work.
void PropertyManager::onDeath(const std::shared_ptr<Attempt>& attempt) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    attempt->died = true;
    if (installed_ == attempt) disconnected_ = true;  // else: an abandoned attempt, ignore
  }
  cv_.notify_all();
}

void PropertyManager::workerLoop() {
  std::unique_lock<std::mutex> lock(mutex_);
  for (;;) {
    cv_.wait(lock, [this] { return stopping_ || disconnected_; });
    if (stopping_) return;

    std::shared_ptr<VehicleServiceProxy> dead = std::move(proxy_);
    proxy_.reset();
    installed_.reset();
    disconnected_ = false;
    ConnectionCallback callback = connectionCallback_;
    lock.unlock();

    dead.reset();
    logWarn(kTag, "vehicle service died; reconnecting");
    if (callback) callback(false);

    // Back off between attempts: the service may take a moment to be restarted by init.
    std::chrono::milliseconds delay = kFirstRetryDelay;
    while (tryConnect() != Status::Ok) {
      lock.lock();
      bool stop = cv_.wait_for(lock, delay, [this] { return stopping_; });
      lock.unlock();
      if (stop) return;
      delay = std::min(delay * 2, kMaxRetryDelay);
    }
    logInfo(kTag, "reconnected to the vehicle service");

    lock.lock();
    callback = connectionCallback_;
    lock.unlock();
    if (callback) callback(true);
    lock.lock();
  }
}

// ---- calls ---------------------------------------------------------------------

Result<PropValue> PropertyManager::getRaw(PropertyId id) {
  auto proxy = currentProxy();
  if (!proxy) return Status::DeadObject;
  return proxy->getProperty(id);
}

Status PropertyManager::setRaw(PropertyId id, const PropValue& value) {
  auto proxy = currentProxy();
  if (!proxy) return Status::DeadObject;
  return proxy->setProperty(id, value);
}

Result<std::vector<PropertyConfig>> PropertyManager::list() {
  auto proxy = currentProxy();
  if (!proxy) return Status::DeadObject;
  return proxy->listProperties();
}

Result<SubscriptionId> PropertyManager::subscribeRaw(PropertyId id, RawCallback callback) {
  // Would wait for a reply that only this (loop) thread can read.
  if (loopThread_->loop().inLoopThread()) return Status::IllegalState;

  std::lock_guard<std::mutex> lock(subscriptionMutex_);
  auto proxy = currentProxy();
  if (!proxy) return Status::DeadObject;

  auto listener = std::make_shared<CallbackListener>(std::move(callback));
  Status s = proxy->subscribe(id, listener);
  if (s != Status::Ok) return s;
  SubscriptionId sid = nextSubscription_++;
  subscriptions_[sid] = Subscription{id, std::move(listener)};
  return sid;
}

Status PropertyManager::unsubscribe(SubscriptionId subscription) {
  if (loopThread_->loop().inLoopThread()) return Status::IllegalState;

  std::lock_guard<std::mutex> lock(subscriptionMutex_);
  auto it = subscriptions_.find(subscription);
  if (it == subscriptions_.end()) return Status::NotFound;
  Subscription sub = std::move(it->second);
  subscriptions_.erase(it);  // gone locally, so a reconnect will not bring it back

  auto proxy = currentProxy();
  if (!proxy) return Status::Ok;  // disconnected: the server-side state died with the connection
  Status s = proxy->unsubscribe(sub.id, sub.listener);
  return s == Status::DeadObject ? Status::Ok : s;
}

}  // namespace mini

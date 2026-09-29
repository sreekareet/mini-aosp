#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "common/paths.h"
#include "framework/property_manager.h"
#include "hal/hal_loader.h"
#include "ipc/rpc_server.h"
#include "ipc/service_manager.h"
#include "services/vehicle/vehicle_service.h"
#include "test_util.h"

using namespace mini;
using namespace std::chrono_literals;

namespace {
const std::string kHalPath = std::string(LIB_DIR) + "/libfake_vehicle_hal.so";

// ServiceManager + vehicle service running on one loop thread inside the test
// process, so ThreadSanitizer sees the real client-thread / loop-thread interaction.
// The HAL is only ever touched from that loop thread (as in the real service).
class InProcessSystem {
 public:
  InProcessSystem() {
    EventLoop& loop = server_.loop();
    auto smListener = Listener::bind(serviceManagerSocketPath());
    hal_ = std::make_unique<LoadedHal>(LoadedHal::load(kHalPath).take());
    service_ = std::make_unique<VehicleService>(hal_->hal());
    stub_ = std::make_unique<VehicleStub>(*service_);
    auto vehicleListener = Listener::bind(serviceSocketPath(kVehicleServiceName));
    smServer_ = std::make_unique<RpcServer>(loop, smListener.take(), sm_);
    vehicleServer_ = std::make_unique<RpcServer>(loop, vehicleListener.take(), *stub_);
    registerService(kVehicleServiceName, serviceSocketPath(kVehicleServiceName), 2s);

    tick_ = [this] {  // fast simulation so tests finish quickly
      hal_->hal().poll();
      server_.loop().postDelayed(20ms, tick_);
    };
    loop.postDelayed(20ms, tick_);
  }

  // Stop the loop thread first: the servers' callbacks must not run while they are destroyed.
  ~InProcessSystem() { server_.stop(); }

 private:
  test::TempRunDir run_;  // first member: the sockets live here
  LoopThread server_;
  ServiceManager sm_;
  std::unique_ptr<LoadedHal> hal_;
  std::unique_ptr<VehicleService> service_;
  std::unique_ptr<VehicleStub> stub_;
  std::unique_ptr<RpcServer> smServer_;
  std::unique_ptr<RpcServer> vehicleServer_;
  std::function<void()> tick_;
};

std::unique_ptr<PropertyManager> connectOrDie() {
  auto pm = PropertyManager::connect();
  EXPECT_TRUE(pm.ok()) << toString(pm.status());
  return pm.ok() ? pm.take() : nullptr;
}

// Collects values delivered on the manager's loop thread, safely readable from the test thread.
struct Collector {
  void add(float v) {
    std::lock_guard<std::mutex> lock(mu);
    values.push_back(v);
  }
  size_t count() {
    std::lock_guard<std::mutex> lock(mu);
    return values.size();
  }
  std::vector<float> snapshot() {
    std::lock_guard<std::mutex> lock(mu);
    return values;
  }
  std::mutex mu;
  std::vector<float> values;
};
}  // namespace

TEST(PropertyManagerTest, TypedGetAndSet) {
  InProcessSystem system;
  auto pm = connectOrDie();
  ASSERT_TRUE(pm);

  ASSERT_EQ(pm->set(kGear, int32_t{2}), Status::Ok);
  auto gear = pm->get(kGear);
  ASSERT_TRUE(gear.ok());
  EXPECT_EQ(gear.value(), 2);
  EXPECT_FLOAT_EQ(pm->get(kFuelLevel).value(), 75.0f);
}

TEST(PropertyManagerTest, ErrorsReachTheCaller) {
  InProcessSystem system;
  auto pm = connectOrDie();
  ASSERT_TRUE(pm);
  EXPECT_EQ(pm->set(kSpeed, 10.0f), Status::ReadOnly);
  EXPECT_EQ(pm->set(kGear, int32_t{99}), Status::InvalidArgument);
  // A key whose type disagrees with the HAL's: speed is a float, not an int.
  EXPECT_EQ(pm->get(PropertyKey<int32_t>{PropertyId::Speed}).status(), Status::InvalidArgument);
}

TEST(PropertyManagerTest, SubscriberSeesUpdatesUntilItUnsubscribes) {
  InProcessSystem system;
  auto pm = connectOrDie();
  ASSERT_TRUE(pm);

  Collector speeds;
  auto sub = pm->subscribe(kSpeed, std::function<void(float)>([&](float v) { speeds.add(v); }));
  ASSERT_TRUE(sub.ok());

  ASSERT_EQ(pm->set(kGear, int32_t{3}), Status::Ok);  // Drive: the simulation accelerates
  ASSERT_TRUE(test::waitFor([&] { return speeds.count() >= 3; }));
  std::vector<float> seen = speeds.snapshot();
  EXPECT_FLOAT_EQ(seen[0], 5.0f);
  EXPECT_LT(seen[0], seen[1]);  // still accelerating, in order

  ASSERT_EQ(pm->unsubscribe(sub.value()), Status::Ok);
  std::this_thread::sleep_for(100ms);  // let in-flight events drain
  size_t after = speeds.count();
  std::this_thread::sleep_for(200ms);
  EXPECT_EQ(speeds.count(), after);  // nothing more arrives
  EXPECT_EQ(pm->unsubscribe(sub.value()), Status::NotFound);
}

TEST(PropertyManagerTest, ManyThreadsMakeCallsConcurrently) {
  InProcessSystem system;
  auto pm = connectOrDie();
  ASSERT_TRUE(pm);

  std::atomic<int> failures{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < 50; ++i) {
        float temp = 16.0f + static_cast<float>((t * 50 + i) % 14);
        if (pm->set(kHvacTemp, temp) != Status::Ok) ++failures;
        if (!pm->get(kHvacTemp).ok()) ++failures;
      }
    });
  }
  for (auto& th : threads) th.join();
  EXPECT_EQ(failures, 0);
}

TEST(PropertyManagerTest, CallingBackIntoTheManagerFromACallbackIsRefused) {
  InProcessSystem system;
  auto pm = connectOrDie();
  ASSERT_TRUE(pm);

  std::atomic<int> result{-1};
  PropertyManager* raw = pm.get();
  auto sub = pm->subscribe(kGear, std::function<void(int32_t)>([&](int32_t) {
    result = static_cast<int>(raw->get(kHvacTemp).status());  // would deadlock without the guard
  }));
  ASSERT_TRUE(sub.ok());
  ASSERT_EQ(pm->set(kGear, int32_t{1}), Status::Ok);
  ASSERT_TRUE(test::waitFor([&] { return result.load() != -1; }));
  EXPECT_EQ(result.load(), static_cast<int>(Status::IllegalState));
}

TEST(PropertyManagerTest, ServiceSurvivesAClientDisconnectingWhileSubscribed) {
  InProcessSystem system;
  {
    auto pm = connectOrDie();
    ASSERT_TRUE(pm);
    ASSERT_TRUE(pm->subscribe(kSpeed, std::function<void(float)>([](float) {})).ok());
    ASSERT_EQ(pm->set(kGear, int32_t{3}), Status::Ok);
    std::this_thread::sleep_for(60ms);  // some events flow to this client
  }  // client vanishes without unsubscribing
  std::this_thread::sleep_for(100ms);  // the service keeps ticking and must clean up quietly

  auto second = connectOrDie();
  ASSERT_TRUE(second);
  EXPECT_TRUE(second->get(kGear).ok());
}

// Same flow across real processes and real sockets.
TEST(PropertyManagerTest, WorksAcrossProcesses) {
  test::TempRunDir run;
  test::ChildProcess sm(std::string(BIN_DIR) + "/servicemanager");
  ASSERT_TRUE(test::waitFor([&] { return test::fileExists(serviceManagerSocketPath()); }));
  test::ChildProcess vehicle(std::string(BIN_DIR) + "/vehicle_service", {"--hal", kHalPath});

  std::unique_ptr<PropertyManager> pm;
  ASSERT_TRUE(test::waitFor([&] {
    auto r = PropertyManager::connect();
    if (r.ok()) pm = r.take();
    return r.ok();
  }));

  Collector speeds;
  ASSERT_TRUE(pm->subscribe(kSpeed, std::function<void(float)>([&](float v) { speeds.add(v); })).ok());
  ASSERT_EQ(pm->set(kGear, int32_t{3}), Status::Ok);
  // The real service ticks every 200 ms, so allow a little time.
  EXPECT_TRUE(test::waitFor([&] { return speeds.count() >= 2; }, 5000));
}

// Service restart, as seen by an app: the manager reconnects and subscriptions resume.
TEST(PropertyManagerTest, ReconnectsAfterServiceRestartAndSubscriptionsResume) {
  test::TempRunDir run;
  test::ChildProcess sm(std::string(BIN_DIR) + "/servicemanager");
  ASSERT_TRUE(test::waitFor([&] { return test::fileExists(serviceManagerSocketPath()); }));
  auto startService = [&] {
    return test::ChildProcess(std::string(BIN_DIR) + "/vehicle_service", {"--hal", kHalPath});
  };
  test::ChildProcess vehicle = startService();

  std::unique_ptr<PropertyManager> pm;
  ASSERT_TRUE(test::waitFor([&] {
    auto r = PropertyManager::connect();
    if (r.ok()) pm = r.take();
    return r.ok();
  }));

  std::mutex mu;
  std::vector<bool> transitions;  // false = died, true = back
  pm->setConnectionCallback([&](bool connected) {
    std::lock_guard<std::mutex> lock(mu);
    transitions.push_back(connected);
  });
  auto transitionCount = [&] {
    std::lock_guard<std::mutex> lock(mu);
    return transitions.size();
  };

  Collector speeds;
  ASSERT_TRUE(pm->subscribe(kSpeed, std::function<void(float)>([&](float v) { speeds.add(v); })).ok());
  ASSERT_EQ(pm->set(kGear, int32_t{3}), Status::Ok);
  ASSERT_TRUE(test::waitFor([&] { return speeds.count() >= 1; }));
  ASSERT_TRUE(pm->isConnected());

  // 1. The service crashes: the death notice arrives and calls fail cleanly.
  vehicle.stop(SIGKILL);
  ASSERT_TRUE(test::waitFor([&] { return transitionCount() >= 1; }));
  EXPECT_FALSE(pm->isConnected());
  EXPECT_EQ(pm->get(kGear).status(), Status::DeadObject);
  EXPECT_EQ(pm->set(kGear, int32_t{1}), Status::DeadObject);
  EXPECT_EQ(pm->subscribe(kFuelLevel, std::function<void(float)>([](float) {})).status(),
            Status::DeadObject);

  // 2. It comes back (init would do this): the manager reconnects on its own.
  vehicle = startService();
  // Wait for the callback itself: isConnected() turns true a moment *before* it is delivered.
  ASSERT_TRUE(test::waitFor([&] { return transitionCount() >= 2; }, 10000));
  EXPECT_TRUE(pm->isConnected());
  {
    std::lock_guard<std::mutex> lock(mu);
    EXPECT_EQ(transitions, (std::vector<bool>{false, true}));
  }
  auto gear = pm->get(kGear);
  ASSERT_TRUE(gear.ok());
  EXPECT_EQ(gear.value(), 0);  // the new process starts from defaults: state was lost

  // 3. The old subscription works again without the app re-subscribing.
  size_t before = speeds.count();
  ASSERT_EQ(pm->set(kGear, int32_t{3}), Status::Ok);
  EXPECT_TRUE(test::waitFor([&] { return speeds.count() > before; }, 5000));
}

TEST(PropertyManagerTest, UnsubscribingWhileDisconnectedSticks) {
  test::TempRunDir run;
  test::ChildProcess sm(std::string(BIN_DIR) + "/servicemanager");
  ASSERT_TRUE(test::waitFor([&] { return test::fileExists(serviceManagerSocketPath()); }));
  test::ChildProcess vehicle(std::string(BIN_DIR) + "/vehicle_service", {"--hal", kHalPath});
  std::unique_ptr<PropertyManager> pm;
  ASSERT_TRUE(test::waitFor([&] {
    auto r = PropertyManager::connect();
    if (r.ok()) pm = r.take();
    return r.ok();
  }));

  Collector speeds;
  auto sub = pm->subscribe(kSpeed, std::function<void(float)>([&](float v) { speeds.add(v); }));
  ASSERT_TRUE(sub.ok());
  vehicle.stop(SIGKILL);
  ASSERT_TRUE(test::waitFor([&] { return !pm->isConnected(); }));
  EXPECT_EQ(pm->unsubscribe(sub.value()), Status::Ok);  // fine while the service is down

  vehicle = test::ChildProcess(std::string(BIN_DIR) + "/vehicle_service", {"--hal", kHalPath});
  ASSERT_TRUE(test::waitFor([&] { return pm->isConnected(); }, 10000));
  ASSERT_EQ(pm->set(kGear, int32_t{3}), Status::Ok);
  std::this_thread::sleep_for(std::chrono::milliseconds(700));
  EXPECT_EQ(speeds.count(), 0u);  // the removed subscription was not resurrected
}

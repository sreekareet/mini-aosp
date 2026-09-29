#include <gtest/gtest.h>

#include "common/paths.h"
#include "hal/hal_loader.h"
#include "ipc/service_manager.h"
#include "services/vehicle/vehicle_service.h"
#include "test_util.h"

using namespace mini;

namespace {
const std::string kHalPath = std::string(LIB_DIR) + "/libfake_vehicle_hal.so";

// Proxy -> (wire encoding) -> stub -> service -> real HAL plugin, all in-process.
class VehicleProxyStubTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto r = LoadedHal::load(kHalPath);
    ASSERT_TRUE(r.ok());
    hal_ = std::make_unique<LoadedHal>(r.take());
    service_ = std::make_unique<VehicleService>(hal_->hal());
    stub_ = std::make_unique<VehicleStub>(*service_);
    auto remote = std::make_unique<test::LoopbackRemote>(*stub_);
    remote_ = remote.get();
    proxy_ = std::make_unique<VehicleServiceProxy>(std::move(remote));
  }
  std::unique_ptr<LoadedHal> hal_;
  std::unique_ptr<VehicleService> service_;
  std::unique_ptr<VehicleStub> stub_;
  test::LoopbackRemote* remote_ = nullptr;  // owned by proxy_
  std::unique_ptr<VehicleServiceProxy> proxy_;
};
}  // namespace

TEST_F(VehicleProxyStubTest, SetThenGet) {
  ASSERT_EQ(proxy_->setProperty(PropertyId::HvacTemp, 24.5f), Status::Ok);
  auto v = proxy_->getProperty(PropertyId::HvacTemp);
  ASSERT_TRUE(v.ok());
  EXPECT_FLOAT_EQ(std::get<float>(v.value()), 24.5f);
}

TEST_F(VehicleProxyStubTest, ListMatchesHal) {
  auto list = proxy_->listProperties();
  ASSERT_TRUE(list.ok());
  EXPECT_EQ(list.value().size(), 4u);
}

TEST_F(VehicleProxyStubTest, HalErrorsTravelBackToTheCaller) {
  EXPECT_EQ(proxy_->setProperty(PropertyId::Speed, 1.0f), Status::ReadOnly);
  EXPECT_EQ(proxy_->setProperty(PropertyId::Gear, int32_t{42}), Status::InvalidArgument);
  EXPECT_EQ(proxy_->getProperty(static_cast<PropertyId>(99)).status(), Status::NotFound);
}

TEST_F(VehicleProxyStubTest, TruncatedRequestIsBadParcel) {
  test::LoopbackRemote remote(*stub_);
  Parcel tooShort;  // kSetProperty wants an id and a value
  tooShort.writeInt32(static_cast<int32_t>(PropertyId::Gear));
  EXPECT_EQ(remote.transact(kSetProperty, tooShort).status(), Status::BadParcel);
}

namespace {
struct RecordingListener final : IPropertyListener {
  void onPropertyChanged(PropertyId id, const PropValue& v) override { events.emplace_back(id, v); }
  std::vector<std::pair<PropertyId, PropValue>> events;
};
}  // namespace

TEST_F(VehicleProxyStubTest, SubscribedListenerSeesChangesUntilUnsubscribed) {
  auto listener = std::make_shared<RecordingListener>();
  ASSERT_EQ(proxy_->subscribe(PropertyId::HvacTemp, listener), Status::Ok);

  proxy_->setProperty(PropertyId::HvacTemp, 22.0f);
  proxy_->setProperty(PropertyId::Gear, int32_t{2});  // different property: not delivered
  ASSERT_EQ(listener->events.size(), 1u);
  EXPECT_EQ(listener->events[0].first, PropertyId::HvacTemp);
  EXPECT_FLOAT_EQ(std::get<float>(listener->events[0].second), 22.0f);

  ASSERT_EQ(proxy_->unsubscribe(PropertyId::HvacTemp, listener), Status::Ok);
  proxy_->setProperty(PropertyId::HvacTemp, 23.0f);
  EXPECT_EQ(listener->events.size(), 1u);
  EXPECT_EQ(proxy_->unsubscribe(PropertyId::HvacTemp, listener), Status::NotFound);
}

TEST_F(VehicleProxyStubTest, SubscribeToUnknownPropertyFails) {
  auto listener = std::make_shared<RecordingListener>();
  EXPECT_EQ(proxy_->subscribe(static_cast<PropertyId>(99), listener), Status::NotFound);
  EXPECT_EQ(proxy_->subscribe(PropertyId::Gear, nullptr), Status::InvalidArgument);
}

TEST_F(VehicleProxyStubTest, TwoListenersOnOnePropertyShareOneServerSubscription) {
  auto a = std::make_shared<RecordingListener>();
  auto b = std::make_shared<RecordingListener>();
  ASSERT_EQ(proxy_->subscribe(PropertyId::Gear, a), Status::Ok);
  ASSERT_EQ(proxy_->subscribe(PropertyId::Gear, b), Status::Ok);
  proxy_->setProperty(PropertyId::Gear, int32_t{1});
  EXPECT_EQ(a->events.size(), 1u);
  EXPECT_EQ(b->events.size(), 1u);
  ASSERT_EQ(proxy_->unsubscribe(PropertyId::Gear, a), Status::Ok);
  proxy_->setProperty(PropertyId::Gear, int32_t{2});
  EXPECT_EQ(a->events.size(), 1u);
  EXPECT_EQ(b->events.size(), 2u);  // b still subscribed
}

TEST_F(VehicleProxyStubTest, ClientGoneDropsItsSubscriptions) {
  auto listener = std::make_shared<RecordingListener>();
  ASSERT_EQ(proxy_->subscribe(PropertyId::Gear, listener), Status::Ok);
  remote_->disconnect();  // the stub is told the connection closed
  proxy_->setProperty(PropertyId::Gear, int32_t{1});
  EXPECT_TRUE(listener->events.empty());
}

TEST_F(VehicleProxyStubTest, OnlyAllowedWritersMaySetProperties) {
  VehicleStub guarded(*service_, UidAllowList::only({42}));
  auto remote = std::make_unique<test::LoopbackRemote>(guarded);
  test::LoopbackRemote* raw = remote.get();
  VehicleServiceProxy proxy(std::move(remote));

  raw->setCaller(43);  // not on the list
  EXPECT_EQ(proxy.setProperty(PropertyId::HvacTemp, 25.0f), Status::PermissionDenied);
  EXPECT_FLOAT_EQ(std::get<float>(proxy.getProperty(PropertyId::HvacTemp).value()), 21.0f);  // unchanged
  auto listener = std::make_shared<RecordingListener>();
  EXPECT_EQ(proxy.subscribe(PropertyId::HvacTemp, listener), Status::Ok);  // reading is open

  raw->setCaller(42);
  EXPECT_EQ(proxy.setProperty(PropertyId::HvacTemp, 25.0f), Status::Ok);
  EXPECT_EQ(listener->events.size(), 1u);
}

// The real thing: separate processes talking over real sockets.
TEST(EndToEndTest, ClientToServiceAcrossProcesses) {
  test::TempRunDir run;
  test::ChildProcess sm(std::string(BIN_DIR) + "/servicemanager");
  ASSERT_TRUE(sm.started());
  ASSERT_TRUE(test::waitFor([&] { return test::fileExists(serviceManagerSocketPath()); }));

  test::ChildProcess vehicle(std::string(BIN_DIR) + "/vehicle_service", {"--hal", kHalPath});
  ASSERT_TRUE(vehicle.started());

  // The service registers itself once it is up; poll the ServiceManager until it appears.
  Result<std::unique_ptr<SocketRemote>> remote = Status::NotFound;
  ASSERT_TRUE(test::waitFor([&] {
    remote = getServiceRemote(kVehicleServiceName);
    return remote.ok();
  }));
  VehicleServiceProxy proxy(remote.take());

  ASSERT_EQ(proxy.setProperty(PropertyId::Gear, int32_t{3}), Status::Ok);
  auto gear = proxy.getProperty(PropertyId::Gear);
  ASSERT_TRUE(gear.ok());
  EXPECT_EQ(std::get<int32_t>(gear.value()), 3);

  // Kill the service: the next call must fail cleanly, not hang or crash us.
  vehicle.stop(SIGKILL);
  EXPECT_EQ(proxy.getProperty(PropertyId::Gear).status(), Status::DeadObject);
}

// The whole identity path: kernel SO_PEERCRED -> RpcServer -> CallerInfo -> stub policy.
TEST(EndToEndTest, ServiceRefusesWritesFromAnUnlistedUid) {
  if (::geteuid() == 0) GTEST_SKIP() << "root is always allowed";
  test::TempRunDir run;
  test::ChildProcess sm(std::string(BIN_DIR) + "/servicemanager");
  ASSERT_TRUE(test::waitFor([&] { return test::fileExists(serviceManagerSocketPath()); }));
  // Only uid 12345 (and root) may write - we are neither.
  test::ChildProcess vehicle(std::string(BIN_DIR) + "/vehicle_service",
                             {"--hal", kHalPath, "--allow-writer", "12345"});

  Result<std::unique_ptr<SocketRemote>> remote = Status::NotFound;
  ASSERT_TRUE(test::waitFor([&] {
    remote = getServiceRemote(kVehicleServiceName);
    return remote.ok();
  }));
  VehicleServiceProxy proxy(remote.take());
  EXPECT_EQ(proxy.setProperty(PropertyId::Gear, int32_t{3}), Status::PermissionDenied);
  auto gear = proxy.getProperty(PropertyId::Gear);
  ASSERT_TRUE(gear.ok());  // reads still work
  EXPECT_EQ(std::get<int32_t>(gear.value()), 0);
}

// By default the user who runs the service may write.
TEST(EndToEndTest, DefaultPolicyAllowsTheServiceOwner) {
  test::TempRunDir run;
  test::ChildProcess sm(std::string(BIN_DIR) + "/servicemanager");
  ASSERT_TRUE(test::waitFor([&] { return test::fileExists(serviceManagerSocketPath()); }));
  test::ChildProcess vehicle(std::string(BIN_DIR) + "/vehicle_service", {"--hal", kHalPath});

  Result<std::unique_ptr<SocketRemote>> remote = Status::NotFound;
  ASSERT_TRUE(test::waitFor([&] {
    remote = getServiceRemote(kVehicleServiceName);
    return remote.ok();
  }));
  VehicleServiceProxy proxy(remote.take());
  EXPECT_EQ(proxy.setProperty(PropertyId::Gear, int32_t{3}), Status::Ok);
}

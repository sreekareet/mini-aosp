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
    proxy_ = std::make_unique<VehicleServiceProxy>(std::make_unique<test::LoopbackRemote>(*stub_));
  }
  std::unique_ptr<LoadedHal> hal_;
  std::unique_ptr<VehicleService> service_;
  std::unique_ptr<VehicleStub> stub_;
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

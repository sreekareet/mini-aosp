#include <gtest/gtest.h>

#include "hal/hal_loader.h"

using namespace mini;

namespace {
const std::string kHalPath = std::string(LIB_DIR) + "/libfake_vehicle_hal.so";

// Every test goes through the real dlopen path, like the service will.
class HalTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto r = LoadedHal::load(kHalPath);
    ASSERT_TRUE(r.ok()) << toString(r.status());
    loaded_ = std::make_unique<LoadedHal>(r.take());
  }
  IVehicleHal& hal() { return loaded_->hal(); }
  std::unique_ptr<LoadedHal> loaded_;
};
}  // namespace

TEST_F(HalTest, ListsAllProperties) { EXPECT_EQ(hal().listProperties().size(), 4u); }

TEST_F(HalTest, SetThenGetWritableProperty) {
  ASSERT_EQ(hal().set(PropertyId::Gear, int32_t{3}), Status::Ok);
  auto v = hal().get(PropertyId::Gear);
  ASSERT_TRUE(v.ok());
  EXPECT_EQ(std::get<int32_t>(v.value()), 3);
}

TEST_F(HalTest, RejectsBadWrites) {
  EXPECT_EQ(hal().set(PropertyId::Speed, 10.0f), Status::ReadOnly);
  EXPECT_EQ(hal().set(PropertyId::Gear, int32_t{9}), Status::InvalidArgument);  // out of range
  EXPECT_EQ(hal().set(PropertyId::Gear, 1.0f), Status::InvalidArgument);        // wrong type
  EXPECT_EQ(hal().set(static_cast<PropertyId>(99), 1.0f), Status::NotFound);
  EXPECT_FALSE(hal().get(static_cast<PropertyId>(99)).ok());
}

TEST_F(HalTest, SetNotifiesOnlyWhenTheValueChanges) {
  std::vector<PropertyId> changed;
  hal().setChangeCallback([&](PropertyId id, const PropValue&) { changed.push_back(id); });
  hal().set(PropertyId::HvacTemp, 25.0f);
  hal().set(PropertyId::HvacTemp, 25.0f);  // same value again: no event
  EXPECT_EQ(changed, std::vector<PropertyId>{PropertyId::HvacTemp});
  hal().setChangeCallback(nullptr);
}

TEST_F(HalTest, PollDrivesTheCarOnlyInDrive) {
  std::vector<float> speeds;
  hal().setChangeCallback([&](PropertyId id, const PropValue& v) {
    if (id == PropertyId::Speed) speeds.push_back(std::get<float>(v));
  });
  hal().poll();  // in Park: nothing moves
  EXPECT_TRUE(speeds.empty());

  hal().set(PropertyId::Gear, int32_t{3});
  hal().poll();
  hal().poll();
  EXPECT_EQ(speeds, (std::vector<float>{5.0f, 10.0f}));

  hal().set(PropertyId::Gear, int32_t{0});  // back to Park: coasts down
  hal().poll();
  EXPECT_EQ(speeds.back(), 5.0f);
  hal().setChangeCallback(nullptr);
}

TEST(HalLoaderTest, MissingLibraryFails) {
  EXPECT_EQ(LoadedHal::load("/no/such/lib.so").status(), Status::IoError);
}

TEST(HalLoaderTest, LibraryWithoutHalSymbolsIsRejected) {
  // libm is a valid shared library but not a vehicle HAL.
  EXPECT_EQ(LoadedHal::load("libm.so.6").status(), Status::NotSupported);
}

TEST(ValueParsingTest, ParsesAndRejects) {
  EXPECT_EQ(std::get<int32_t>(*parseValue(ValueType::Int32, "42")), 42);
  EXPECT_FLOAT_EQ(std::get<float>(*parseValue(ValueType::Float, "21.5")), 21.5f);
  EXPECT_FALSE(parseValue(ValueType::Int32, "4x").has_value());
  EXPECT_FALSE(parseValue(ValueType::Bool, "maybe").has_value());
  EXPECT_EQ(parsePropertyId("hvac_temp"), PropertyId::HvacTemp);
  EXPECT_FALSE(parsePropertyId("warp_drive").has_value());
}

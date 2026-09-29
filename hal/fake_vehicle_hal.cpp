// The "vendor" side: a fake car, built as libfake_vehicle_hal.so.
#include <map>
#include <memory>

#include "hal/vehicle_hal.h"

namespace mini {
namespace {

class FakeVehicleHal final : public IVehicleHal {
 public:
  FakeVehicleHal() {
    add({PropertyId::Speed, ValueType::Float, false}, 0.0f, 0, 300);
    add({PropertyId::FuelLevel, ValueType::Float, false}, 75.0f, 0, 100);
    add({PropertyId::Gear, ValueType::Int32, true}, int32_t{0}, 0, 3);
    add({PropertyId::HvacTemp, ValueType::Float, true}, 21.0f, 16, 30);
  }

  std::vector<PropertyConfig> listProperties() const override {
    std::vector<PropertyConfig> out;
    for (const auto& [id, entry] : props_) out.push_back(entry.config);
    return out;
  }

  Result<PropValue> get(PropertyId id) const override {
    auto it = props_.find(id);
    if (it == props_.end()) return Status::NotFound;
    return it->second.value;
  }

  Status set(PropertyId id, const PropValue& value) override {
    auto it = props_.find(id);
    if (it == props_.end()) return Status::NotFound;
    Entry& e = it->second;
    if (!e.config.writable) return Status::ReadOnly;
    if (typeOf(value) != e.config.type) return Status::InvalidArgument;
    if (std::optional<double> d = numeric(value)) {
      if (*d < e.min || *d > e.max) return Status::InvalidArgument;
    }
    e.value = value;
    return Status::Ok;
  }

 private:
  struct Entry {
    PropertyConfig config;
    PropValue value;
    double min;
    double max;
  };

  void add(PropertyConfig config, PropValue initial, double min, double max) {
    props_[config.id] = Entry{config, initial, min, max};
  }

  // Numeric view of a value for range checks (bools have no range).
  static std::optional<double> numeric(const PropValue& v) {
    if (const int32_t* i = std::get_if<int32_t>(&v)) return *i;
    if (const float* f = std::get_if<float>(&v)) return *f;
    return std::nullopt;
  }

  std::map<PropertyId, Entry> props_;
};

}  // namespace
}  // namespace mini

// The plugin entry points. Default visibility is spelled out so this still works
// if the library is ever built with -fvisibility=hidden.
extern "C" {

__attribute__((visibility("default"))) int vehicleHalAbiVersion() {
  return mini::kVehicleHalAbiVersion;
}

// A C signature cannot return unique_ptr, so ownership crosses the boundary as a
// raw pointer; the loader re-wraps it in a unique_ptr right away.
__attribute__((visibility("default"))) mini::IVehicleHal* createVehicleHal() {
  return std::make_unique<mini::FakeVehicleHal>().release();
}

__attribute__((visibility("default"))) void destroyVehicleHal(mini::IVehicleHal* hal) {
  std::unique_ptr<mini::IVehicleHal> owner(hal);  // deleted here, in the .so that allocated it
}
}

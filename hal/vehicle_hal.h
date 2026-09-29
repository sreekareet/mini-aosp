// The HAL contract: the only thing the rest of the system knows about "the car".
// A vendor ships a shared library that implements IVehicleHal; the platform loads
// it at runtime (see hal_loader.h). Real AOSP: an AIDL/HIDL vehicle HAL.
#pragma once

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "common/result.h"

namespace mini {

// enum class: no implicit int conversion, no name clashes. Values are part of the
// wire format later on, so never renumber them.
enum class PropertyId : int32_t {
  Speed = 1,     // float, km/h, read-only
  FuelLevel = 2, // float, percent, read-only
  Gear = 3,      // int32: 0=P 1=R 2=N 3=D, writable
  HvacTemp = 4,  // float, deg C, writable
};

inline constexpr PropertyId kAllPropertyIds[] = {PropertyId::Speed, PropertyId::FuelLevel,
                                                 PropertyId::Gear, PropertyId::HvacTemp};

enum class ValueType : int32_t { Bool = 0, Int32 = 1, Float = 2 };

// A sum type: a property value is exactly one of these. The alternative order
// matches ValueType so typeOf() is just the variant index.
using PropValue = std::variant<bool, int32_t, float>;

inline ValueType typeOf(const PropValue& v) { return static_cast<ValueType>(v.index()); }

struct PropertyConfig {
  PropertyId id;
  ValueType type;
  bool writable;
};

inline const char* toString(PropertyId id) {
  switch (id) {
    case PropertyId::Speed: return "speed";
    case PropertyId::FuelLevel: return "fuel_level";
    case PropertyId::Gear: return "gear";
    case PropertyId::HvacTemp: return "hvac_temp";
  }
  return "unknown";
}

inline std::optional<PropertyId> parsePropertyId(std::string_view name) {
  for (PropertyId id : kAllPropertyIds) {
    if (name == toString(id)) return id;
  }
  return std::nullopt;
}

inline std::string toString(const PropValue& v) {
  if (const bool* b = std::get_if<bool>(&v)) return *b ? "true" : "false";
  if (const int32_t* i = std::get_if<int32_t>(&v)) return std::to_string(*i);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(std::get<float>(v)));
  return buf;
}

// Parse user text into a value of the requested type; nullopt if it does not fit.
inline std::optional<PropValue> parseValue(ValueType type, std::string_view text) {
  const char* first = text.data();
  const char* last = text.data() + text.size();
  switch (type) {
    case ValueType::Bool:
      if (text == "true" || text == "1") return PropValue{true};
      if (text == "false" || text == "0") return PropValue{false};
      return std::nullopt;
    case ValueType::Int32: {
      int32_t v = 0;
      auto r = std::from_chars(first, last, v);
      if (r.ec != std::errc() || r.ptr != last) return std::nullopt;
      return PropValue{v};
    }
    case ValueType::Float: {
      float v = 0;
      auto r = std::from_chars(first, last, v);
      if (r.ec != std::errc() || r.ptr != last) return std::nullopt;
      return PropValue{v};
    }
  }
  return std::nullopt;
}

// Abstract interface with a virtual destructor, so implementations can be
// destroyed through a base pointer.
class IVehicleHal {
 public:
  virtual ~IVehicleHal() = default;

  virtual std::vector<PropertyConfig> listProperties() const = 0;
  virtual Result<PropValue> get(PropertyId id) const = 0;
  // NotFound: unknown id. ReadOnly: not writable. InvalidArgument: wrong type/range.
  virtual Status set(PropertyId id, const PropValue& value) = 0;

  // TODO(stage 3): change callbacks and poll() so the service can push events.
};

// ---- Plugin ABI -----------------------------------------------------------
// A HAL library must export these three C symbols. `extern "C"` keeps the names
// unmangled, so dlsym() can find them without knowing the compiler's scheme.
// The interface itself is a C++ virtual class, so plugin and loader must be built
// with the same compiler/stdlib. (Real HALs solve this with AIDL/Binder; a C ABI
// or PIMPL is the other classic answer - see docs/stages/stage-1.md.)
inline constexpr int kVehicleHalAbiVersion = 1;
inline constexpr const char* kSymAbiVersion = "vehicleHalAbiVersion";
inline constexpr const char* kSymCreate = "createVehicleHal";
inline constexpr const char* kSymDestroy = "destroyVehicleHal";

using AbiVersionFn = int (*)();
using CreateHalFn = IVehicleHal* (*)();
using DestroyHalFn = void (*)(IVehicleHal*);

}  // namespace mini

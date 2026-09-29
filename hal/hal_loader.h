// Loading a HAL plugin with dlopen, wrapped in RAII types.
#pragma once

#include <memory>
#include <string>

#include "common/result.h"
#include "hal/vehicle_hal.h"

namespace mini {

// Owns a dlopen() handle; dlclose() on destruction. Move-only.
class DlHandle {
 public:
  static Result<DlHandle> open(const std::string& path);

  DlHandle(DlHandle&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
  DlHandle& operator=(DlHandle&& other) noexcept;
  DlHandle(const DlHandle&) = delete;
  DlHandle& operator=(const DlHandle&) = delete;
  ~DlHandle();

  void* symbol(const char* name) const;

 private:
  explicit DlHandle(void* handle) : handle_(handle) {}
  void* handle_ = nullptr;
};

// A HAL instance together with the library its code lives in.
//
// Lifetime rule: the object's destructor is code inside the .so, so the .so must
// stay loaded until the object is gone. Members are destroyed in reverse
// declaration order, so declaring `lib_` first guarantees hal_ dies before dlclose.
class LoadedHal {
 public:
  static Result<LoadedHal> load(const std::string& path);

  LoadedHal(LoadedHal&&) = default;
  // Defaulted move-assign would replace lib_ (dlclose) *before* hal_, running
  // the old HAL's destructor from an unmapped library. So: no assignment.
  LoadedHal& operator=(LoadedHal&&) = delete;

  IVehicleHal& hal() { return *hal_; }
  const IVehicleHal& hal() const { return *hal_; }

 private:
  using Deleter = void (*)(IVehicleHal*);  // the plugin's own destroy function
  LoadedHal(DlHandle lib, std::unique_ptr<IVehicleHal, Deleter> hal)
      : lib_(std::move(lib)), hal_(std::move(hal)) {}

  DlHandle lib_;
  std::unique_ptr<IVehicleHal, Deleter> hal_;
};

}  // namespace mini

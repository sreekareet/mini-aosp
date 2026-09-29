#include "hal/hal_loader.h"

#include <dlfcn.h>

#include "common/log.h"

namespace mini {

namespace {
constexpr const char* kTag = "HalLoader";
}

Result<DlHandle> DlHandle::open(const std::string& path) {
  // RTLD_NOW: resolve all symbols now, so a broken plugin fails here, not later.
  // RTLD_LOCAL: keep the plugin's symbols out of the global namespace.
  void* h = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (h == nullptr) {
    logError(kTag, "dlopen(%s) failed: %s", path.c_str(), ::dlerror());
    return Status::IoError;
  }
  return DlHandle(h);
}

DlHandle& DlHandle::operator=(DlHandle&& other) noexcept {
  if (this != &other) {
    if (handle_ != nullptr) ::dlclose(handle_);
    handle_ = std::exchange(other.handle_, nullptr);
  }
  return *this;
}

DlHandle::~DlHandle() {
  if (handle_ != nullptr) ::dlclose(handle_);
}

void* DlHandle::symbol(const char* name) const { return ::dlsym(handle_, name); }

Result<LoadedHal> LoadedHal::load(const std::string& path) {
  Result<DlHandle> lib = DlHandle::open(path);
  if (!lib) return lib.status();

  // dlsym returns void*; converting to a function pointer is conditionally
  // supported in C++ (fine on Linux, and what POSIX requires).
  auto abi = reinterpret_cast<AbiVersionFn>(lib.value().symbol(kSymAbiVersion));
  auto create = reinterpret_cast<CreateHalFn>(lib.value().symbol(kSymCreate));
  auto destroy = reinterpret_cast<DestroyHalFn>(lib.value().symbol(kSymDestroy));
  if (abi == nullptr || create == nullptr || destroy == nullptr) {
    logError(kTag, "%s is not a vehicle HAL (missing symbols)", path.c_str());
    return Status::NotSupported;
  }
  if (abi() != kVehicleHalAbiVersion) {
    logError(kTag, "%s has ABI %d, expected %d", path.c_str(), abi(), kVehicleHalAbiVersion);
    return Status::NotSupported;
  }

  // Take ownership immediately so nothing can leak between create and return.
  std::unique_ptr<IVehicleHal, Deleter> hal(create(), destroy);
  if (!hal) return Status::IoError;
  return LoadedHal(lib.take(), std::move(hal));
}

}  // namespace mini

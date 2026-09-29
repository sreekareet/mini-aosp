// Stage 1 demo: everything in one process. Load the HAL plugin, read and write.
#include <cstdio>

#include "common/paths.h"
#include "hal/hal_loader.h"

using namespace mini;

static void dump(const IVehicleHal& hal) {
  for (const PropertyConfig& c : hal.listProperties()) {
    Result<PropValue> v = hal.get(c.id);
    std::printf("  %-11s = %-6s (%s)\n", toString(c.id), v ? toString(v.value()).c_str() : "?",
                c.writable ? "rw" : "ro");
  }
}

int main(int argc, char** argv) {
  std::string path = argc > 1 ? argv[1] : defaultHalPath();
  Result<LoadedHal> loaded = LoadedHal::load(path);
  if (!loaded) {
    std::fprintf(stderr, "cannot load HAL '%s': %s\n", path.c_str(), toString(loaded.status()));
    return 1;
  }
  IVehicleHal& hal = loaded.value().hal();

  std::printf("initial state:\n");
  dump(hal);

  std::printf("set gear=3 -> %s\n", toString(hal.set(PropertyId::Gear, int32_t{3})));
  std::printf("set speed=50 -> %s\n", toString(hal.set(PropertyId::Speed, 50.0f)));
  std::printf("set gear=9 -> %s\n", toString(hal.set(PropertyId::Gear, int32_t{9})));
  std::printf("after:\n");
  dump(hal);
  return 0;
}

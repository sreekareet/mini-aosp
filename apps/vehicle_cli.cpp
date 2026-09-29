// Stage 2 client: talks to the vehicle service through the proxy.
//   vehicle_cli list | get <property> | set <property> <value>
// TODO(stage 3): switch to the framework PropertyManager.
#include <cstdio>
#include <cstring>

#include "ipc/service_manager.h"
#include "services/vehicle/vehicle_iface.h"

using namespace mini;

static int usage() {
  std::fprintf(stderr, "usage: vehicle_cli list | get <prop> | set <prop> <value>\n");
  return 2;
}

int main(int argc, char** argv) {
  if (argc < 2) return usage();

  auto remote = getServiceRemote(kVehicleServiceName);
  if (!remote) {
    std::fprintf(stderr, "cannot reach vehicle service: %s\n", toString(remote.status()));
    return 1;
  }
  VehicleServiceProxy vehicle(remote.take());

  std::string cmd = argv[1];
  if (cmd == "list") {
    auto list = vehicle.listProperties();
    if (!list) return std::fprintf(stderr, "list failed: %s\n", toString(list.status())), 1;
    for (const PropertyConfig& c : list.value()) {
      auto v = vehicle.getProperty(c.id);
      std::printf("%-11s = %-6s (%s)\n", toString(c.id), v ? toString(v.value()).c_str() : "?",
                  c.writable ? "rw" : "ro");
    }
    return 0;
  }

  if (argc < 3) return usage();
  std::optional<PropertyId> id = parsePropertyId(argv[2]);
  if (!id) return std::fprintf(stderr, "unknown property '%s'\n", argv[2]), 2;

  if (cmd == "get" && argc == 3) {
    auto v = vehicle.getProperty(*id);
    if (!v) return std::fprintf(stderr, "get failed: %s\n", toString(v.status())), 1;
    std::printf("%s\n", toString(v.value()).c_str());
    return 0;
  }
  if (cmd == "set" && argc == 4) {
    // The CLI doesn't hardcode types: it asks the service what type the property has.
    auto list = vehicle.listProperties();
    if (!list) return std::fprintf(stderr, "list failed: %s\n", toString(list.status())), 1;
    for (const PropertyConfig& c : list.value()) {
      if (c.id != *id) continue;
      std::optional<PropValue> value = parseValue(c.type, argv[3]);
      if (!value) return std::fprintf(stderr, "bad value '%s'\n", argv[3]), 2;
      Status s = vehicle.setProperty(*id, *value);
      std::printf("%s\n", toString(s));
      return s == Status::Ok ? 0 : 1;
    }
    return std::fprintf(stderr, "property not offered by the HAL\n"), 1;
  }
  return usage();
}

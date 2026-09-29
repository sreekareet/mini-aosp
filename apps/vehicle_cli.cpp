// Client app, written against the framework API only (no sockets, no parcels).
//   vehicle_cli list | get <prop> | set <prop> <value> | watch <prop> [seconds]
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common/signal_fd.h"
#include "framework/property_manager.h"

using namespace mini;

static int usage() {
  std::fprintf(stderr, "usage: vehicle_cli list | get <prop> | set <prop> <value> | watch <prop> [seconds]\n");
  return 2;
}

static int fail(const char* what, Status s) {
  std::fprintf(stderr, "%s failed: %s\n", what, toString(s));
  return 1;
}

int main(int argc, char** argv) {
  SignalFd signals({SIGINT, SIGTERM, SIGALRM});  // before PropertyManager starts its thread
  if (argc < 2) return usage();

  auto manager = PropertyManager::connect();
  if (!manager) return fail("connect", manager.status());
  PropertyManager& pm = *manager.value();
  std::string cmd = argv[1];

  if (cmd == "list") {
    auto list = pm.list();
    if (!list) return fail("list", list.status());
    for (const PropertyConfig& c : list.value()) {
      auto v = pm.getRaw(c.id);
      std::printf("%-11s = %-6s (%s)\n", toString(c.id), v ? toString(v.value()).c_str() : "?",
                  c.writable ? "rw" : "ro");
    }
    return 0;
  }

  if (argc < 3) return usage();
  std::optional<PropertyId> id = parsePropertyId(argv[2]);
  if (!id) {
    std::fprintf(stderr, "unknown property '%s'\n", argv[2]);
    return 2;
  }

  if (cmd == "get" && argc == 3) {
    auto v = pm.getRaw(*id);
    if (!v) return fail("get", v.status());
    std::printf("%s\n", toString(v.value()).c_str());
    return 0;
  }

  if (cmd == "set" && argc == 4) {
    // The CLI doesn't hardcode types: it asks the service what type the property has.
    auto list = pm.list();
    if (!list) return fail("list", list.status());
    for (const PropertyConfig& c : list.value()) {
      if (c.id != *id) continue;
      std::optional<PropValue> value = parseValue(c.type, argv[3]);
      if (!value) {
        std::fprintf(stderr, "bad value '%s'\n", argv[3]);
        return 2;
      }
      Status s = pm.setRaw(*id, *value);
      std::printf("%s\n", toString(s));
      return s == Status::Ok ? 0 : 1;
    }
    std::fprintf(stderr, "property not offered by the HAL\n");
    return 1;
  }

  if (cmd == "watch" && (argc == 3 || argc == 4)) {
    auto sub = pm.subscribeRaw(*id, [](PropertyId changed, const PropValue& v) {
      std::printf("%s = %s\n", toString(changed), toString(v).c_str());
      std::fflush(stdout);
    });
    if (!sub) return fail("subscribe", sub.status());
    // The manager survives a service restart: tell the user, subscriptions resume by themselves.
    pm.setConnectionCallback([](bool connected) {
      std::printf("[%s]\n", connected ? "service back" : "service died");
      std::fflush(stdout);
    });
    if (argc == 4) alarm(static_cast<unsigned>(std::atoi(argv[3])));  // optional time limit
    signals.wait();  // Ctrl-C, SIGTERM, or the alarm
    pm.unsubscribe(sub.value());
    return 0;
  }
  return usage();
}

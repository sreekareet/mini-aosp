// init: the first process. Reads init.conf, runs the property service, starts the
// services, and restarts them when they crash. Stand-in for Android's init.
#include <fstream>
#include <sstream>

#include "common/event_loop.h"
#include "common/log.h"
#include "common/paths.h"
#include "common/signal_fd.h"
#include "init/config.h"
#include "init/property_service.h"
#include "init/supervisor.h"
#include "ipc/rpc_server.h"

using namespace mini;

int main(int argc, char** argv) {
  // Signals become fd events. Must be first: it blocks the signals before any child exists.
  SignalFd signals({SIGCHLD, SIGINT, SIGTERM});

  std::string configPath = argc > 1 ? argv[1] : exeDir() + "/../etc/init.conf";
  std::ifstream file(configPath);
  if (!file) {
    logError("init", "cannot read %s", configPath.c_str());
    return 1;
  }
  std::stringstream text;
  text << file.rdbuf();
  std::string error;
  Result<InitConfig> config = parseInitConfig(text.str(), error);
  if (!config) {
    logError("init", "%s: %s", configPath.c_str(), error.c_str());
    return 1;
  }

  EventLoop loop;
  PropertyStore props;
  for (const auto& [k, v] : config.value().properties) props.set(k, v);

  // The property service, served from init's own loop.
  PropertyServiceStub propertyStub(props);
  Result<Listener> listener = Listener::bind(serviceSocketPath(kPropertyServiceName));
  if (!listener) return 1;
  RpcServer propertyServer(loop, listener.take(), propertyStub);

  Supervisor supervisor(loop, props);
  for (ServiceDef& def : config.value().services) supervisor.addService(std::move(def));

  loop.addFd(signals.fd(), EPOLLIN, [&](uint32_t) {
    while (std::optional<int> sig = signals.read()) {
      if (*sig == SIGCHLD) supervisor.onChildExit();
      else supervisor.stopAll();  // SIGINT / SIGTERM
    }
  });

  supervisor.startAll();
  props.set("sys.boot_completed", "1");
  logInfo("init", "boot completed");
  loop.run();
  logInfo("init", "exiting");
  return 0;
}

// The service process: load the HAL, serve it over a socket, register with the
// ServiceManager. Stand-in for system_server hosting CarService.
#include <chrono>
#include <cstring>
#include <functional>

#include "common/event_loop.h"
#include "common/log.h"
#include "common/paths.h"
#include "common/signal_fd.h"
#include "hal/hal_loader.h"
#include "ipc/rpc_server.h"
#include "ipc/service_manager.h"
#include "services/vehicle/vehicle_service.h"

using namespace mini;

int main(int argc, char** argv) {
  std::string halPath = defaultHalPath();
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--hal") == 0) halPath = argv[i + 1];
  }
  SignalFd signals({SIGINT, SIGTERM});  // first, before any thread exists

  // Destruction runs in reverse order: server (stops using the loop), stub, service,
  // HAL, and the .so is unloaded last. The loop outlives everything that uses it.
  EventLoop loop;
  quitOnSignal(loop, signals);

  Result<LoadedHal> hal = LoadedHal::load(halPath);
  if (!hal) return 1;
  VehicleService service(hal.value().hal());
  VehicleStub stub(service);

  std::string socketPath = serviceSocketPath(kVehicleServiceName);
  Result<Listener> listener = Listener::bind(socketPath);
  if (!listener) return 1;
  RpcServer server(loop, listener.take(), stub);

  // Wait for the ServiceManager: whoever starts us may not have started it yet.
  Status s = registerService(kVehicleServiceName, socketPath, std::chrono::seconds(10));
  if (s != Status::Ok) {
    logError("vehicle", "cannot register with servicemanager: %s", toString(s));
    return 1;
  }

  // The HAL's simulation ticks on the same thread as everything else, so the HAL
  // callback -> listener -> socket path needs no locking.
  std::function<void()> tick = [&] {
    hal.value().hal().poll();
    loop.postDelayed(std::chrono::milliseconds(200), tick);
  };
  loop.postDelayed(std::chrono::milliseconds(200), tick);

  logInfo("vehicle", "service ready (%s)", socketPath.c_str());
  loop.run();
  logInfo("vehicle", "stopping");
  return 0;
}

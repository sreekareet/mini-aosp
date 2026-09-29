// The service process: load the HAL, serve it over a socket, register with the
// ServiceManager. Stand-in for system_server hosting CarService.
#include <cstring>

#include "common/log.h"
#include "common/paths.h"
#include "common/stop_signal.h"
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
  installStopHandler();

  // Order matters for destruction (reverse of construction): the server dies first,
  // then the stub and service, then the HAL, and the .so is unloaded last.
  Result<LoadedHal> hal = LoadedHal::load(halPath);
  if (!hal) return 1;
  VehicleService service(hal.value().hal());
  VehicleStub stub(service);

  std::string socketPath = serviceSocketPath(kVehicleServiceName);
  Result<Listener> listener = Listener::bind(socketPath);
  if (!listener) return 1;
  RpcServer server(listener.take(), stub);

  // Wait for the ServiceManager: whoever starts us may not have started it yet.
  Status s = registerService(kVehicleServiceName, socketPath, std::chrono::seconds(10));
  if (s != Status::Ok) {
    logError("vehicle", "cannot register with servicemanager: %s", toString(s));
    return 1;
  }
  logInfo("vehicle", "service ready (%s)", socketPath.c_str());

  while (!stopRequested()) server.serveOnce(500);
  logInfo("vehicle", "stopping");
  return 0;
}

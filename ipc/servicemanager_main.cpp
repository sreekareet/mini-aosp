// The servicemanager process.
#include "common/log.h"
#include "common/paths.h"
#include "common/stop_signal.h"
#include "ipc/rpc_server.h"
#include "ipc/service_manager.h"

using namespace mini;

int main() {
  installStopHandler();
  Result<Listener> listener = Listener::bind(serviceManagerSocketPath());
  if (!listener) {
    logError("servicemanager", "cannot listen on %s", serviceManagerSocketPath().c_str());
    return 1;
  }
  ServiceManager sm;
  RpcServer server(listener.take(), sm);
  logInfo("servicemanager", "ready on %s", serviceManagerSocketPath().c_str());
  while (!stopRequested()) server.serveOnce(500);
  logInfo("servicemanager", "stopping");
  return 0;
}

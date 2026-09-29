// The servicemanager process.
#include "common/event_loop.h"
#include "common/log.h"
#include "common/paths.h"
#include "common/signal_fd.h"
#include "ipc/rpc_server.h"
#include "ipc/service_manager.h"

using namespace mini;

int main() {
  SignalFd signals({SIGINT, SIGTERM});  // first, before any thread exists
  Result<Listener> listener = Listener::bind(serviceManagerSocketPath());
  if (!listener) {
    logError("servicemanager", "cannot listen on %s", serviceManagerSocketPath().c_str());
    return 1;
  }
  EventLoop loop;
  quitOnSignal(loop, signals);
  // Only the user that runs the system (and root) may register services.
  ServiceManager sm(UidAllowList::only({::geteuid()}));
  RpcServer server(loop, listener.take(), sm);
  logInfo("servicemanager", "ready on %s", serviceManagerSocketPath().c_str());
  loop.run();
  logInfo("servicemanager", "stopping");
  return 0;
}

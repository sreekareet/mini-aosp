// Serves one IStub on a Unix socket. Single-threaded: one poll() loop handles the
// listening socket and every client connection.
// TODO(stage 3): replace the hand-written poll() with the shared epoll EventLoop.
#pragma once

#include <map>

#include "ipc/stub.h"
#include "ipc/transport.h"

namespace mini {

class RpcServer {
 public:
  RpcServer(Listener listener, IStub& stub) : listener_(std::move(listener)), stub_(stub) {}

  // Wait up to timeoutMs for activity, then handle everything that is ready.
  // Call it in a loop; between calls the caller can check its stop flag.
  void serveOnce(int timeoutMs);

 private:
  void acceptClients();
  // Returns false if the connection should be dropped.
  bool serveClient(ConnectionId id, Connection& conn);

  Listener listener_;
  IStub& stub_;
  std::map<ConnectionId, Connection> clients_;
  ConnectionId nextId_ = 1;
};

}  // namespace mini

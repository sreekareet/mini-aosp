// Serves one IStub on a Unix socket, driven by an EventLoop: the listening socket
// and every client connection are fds registered on the loop.
#pragma once

#include <map>

#include "common/event_loop.h"
#include "ipc/stub.h"
#include "ipc/transport.h"

namespace mini {

// All work happens on the loop thread, so the server and the stub it calls need no
// locks. Destroy the server only when the loop is not running (or from the loop
// thread): its fd callbacks capture `this`.
class RpcServer {
 public:
  RpcServer(EventLoop& loop, Listener listener, IStub& stub);
  ~RpcServer();
  RpcServer(const RpcServer&) = delete;
  RpcServer& operator=(const RpcServer&) = delete;

 private:
  struct Client {
    Connection conn;
    PeerCredentials peer;  // fixed when the client connected
  };

  void onListenerReadable();
  void onClientReadable(ConnectionId id);
  bool serveClient(ConnectionId id, Client& client);  // false: drop this client
  void dropClient(ConnectionId id);
  Status sendEvent(ConnectionId id, uint32_t code, const Parcel& args);

  EventLoop& loop_;
  Listener listener_;
  IStub& stub_;
  std::map<ConnectionId, Client> clients_;
  ConnectionId nextId_ = 1;
};

}  // namespace mini

#include "ipc/rpc_server.h"

#include "common/log.h"
#include "ipc/wire.h"

namespace mini {

namespace {
constexpr const char* kTag = "RpcServer";
}

RpcServer::RpcServer(EventLoop& loop, Listener listener, IStub& stub)
    : loop_(loop), listener_(std::move(listener)), stub_(stub) {
  Status s = loop_.addFd(listener_.fd(), EPOLLIN, [this](uint32_t) { onListenerReadable(); });
  if (s != Status::Ok) logError(kTag, "cannot watch listener: %s", toString(s));
}

RpcServer::~RpcServer() {
  loop_.removeFd(listener_.fd());
  for (const auto& [id, client] : clients_) loop_.removeFd(client.conn.fd());
}

void RpcServer::onListenerReadable() {
  for (;;) {
    Result<Connection> c = listener_.accept();
    if (!c) return;  // WouldBlock: no more pending clients
    ConnectionId id = nextId_++;
    Client client{c.take(), PeerCredentials{}};
    Result<PeerCredentials> peer = client.conn.peerCredentials();
    if (peer) client.peer = peer.value();  // else stays "unknown": matches no allow-list
    auto it = clients_.emplace(id, std::move(client)).first;
    loop_.addFd(it->second.conn.fd(), EPOLLIN, [this, id](uint32_t) { onClientReadable(id); });
  }
}

void RpcServer::onClientReadable(ConnectionId id) {
  auto it = clients_.find(id);
  if (it == clients_.end()) return;
  if (!serveClient(id, it->second)) dropClient(id);
}

bool RpcServer::serveClient(ConnectionId id, Client& client) {
  Connection& conn = client.conn;
  Status fillStatus = conn.fill();

  // Answer every complete frame that has arrived (a client may pipeline several).
  for (;;) {
    Result<Parcel> frame = conn.takeFrame();
    if (frame.status() == Status::WouldBlock) break;
    if (!frame) return false;  // oversize frame: the peer is broken, drop it

    Parcel call = frame.take();
    Result<uint32_t> code = parseCall(call);
    CallerInfo caller;
    caller.connection = id;
    caller.uid = client.peer.uid;
    caller.pid = client.peer.pid;
    caller.sendEvent = [this, id](uint32_t evCode, const Parcel& args) {
      return sendEvent(id, evCode, args);
    };
    Parcel reply;
    Status status = code ? stub_.onTransact(code.value(), call, reply, caller) : Status::BadParcel;
    if (conn.sendFrame(makeReply(status, reply)) != Status::Ok) return false;
  }

  // Frames that arrived just before EOF were answered above; now drop the client.
  return fillStatus == Status::Ok || fillStatus == Status::WouldBlock;
}

void RpcServer::dropClient(ConnectionId id) {
  auto it = clients_.find(id);
  if (it == clients_.end()) return;
  loop_.removeFd(it->second.conn.fd());
  clients_.erase(it);  // closes the socket
  stub_.onClientGone(id);
}

Status RpcServer::sendEvent(ConnectionId id, uint32_t code, const Parcel& args) {
  auto it = clients_.find(id);
  if (it == clients_.end()) return Status::NotFound;
  // A dead or stuck client is not fatal here: its read side notices and drops it.
  return it->second.conn.sendFrame(makeEvent(code, args));
}

}  // namespace mini

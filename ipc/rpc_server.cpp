#include "ipc/rpc_server.h"

#include <poll.h>

#include <cerrno>
#include <vector>

#include "common/log.h"
#include "ipc/wire.h"

namespace mini {

namespace {
constexpr const char* kTag = "RpcServer";
}

void RpcServer::serveOnce(int timeoutMs) {
  std::vector<pollfd> fds;
  std::vector<ConnectionId> ids;  // ids[i] belongs to fds[i + 1]
  fds.push_back({listener_.fd(), POLLIN, 0});
  for (const auto& [id, conn] : clients_) {
    fds.push_back({conn.fd(), POLLIN, 0});
    ids.push_back(id);
  }

  if (::poll(fds.data(), fds.size(), timeoutMs) <= 0) return;  // timeout or EINTR (signal)

  if (fds[0].revents & POLLIN) acceptClients();

  std::vector<ConnectionId> dead;
  for (size_t i = 0; i < ids.size(); ++i) {
    if (fds[i + 1].revents == 0) continue;
    if (!serveClient(ids[i], clients_.at(ids[i]))) dead.push_back(ids[i]);
  }
  for (ConnectionId id : dead) clients_.erase(id);  // Connection's destructor closes the fd
}

void RpcServer::acceptClients() {
  for (;;) {
    Result<Connection> c = listener_.accept();
    if (!c) return;  // WouldBlock: no more pending clients
    clients_.emplace(nextId_++, c.take());
  }
}

bool RpcServer::serveClient(ConnectionId id, Connection& conn) {
  Status fillStatus = conn.fill();

  // Answer every complete frame that has arrived (a client may pipeline several).
  for (;;) {
    Result<Parcel> frame = conn.takeFrame();
    if (frame.status() == Status::WouldBlock) break;
    if (!frame) return false;  // oversize frame: the peer is broken, drop it

    Parcel call = frame.take();
    Result<uint32_t> code = parseCall(call);
    Parcel reply;
    Status status = code ? stub_.onTransact(code.value(), call, reply, CallerInfo{id})
                         : Status::BadParcel;
    if (conn.sendFrame(makeReply(status, reply)) != Status::Ok) return false;
  }

  // Frames that arrived just before EOF were answered above; now drop the client.
  return fillStatus == Status::Ok || fillStatus == Status::WouldBlock;
}

}  // namespace mini

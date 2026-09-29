#include "ipc/remote.h"

#include "ipc/wire.h"

namespace mini {

Result<std::unique_ptr<SocketRemote>> SocketRemote::connect(const std::string& path) {
  Result<Connection> conn = Connection::connectTo(path);
  if (!conn) return conn.status();
  return std::make_unique<SocketRemote>(conn.take());
}

Result<Parcel> SocketRemote::transact(uint32_t code, const Parcel& data) {
  Status s = conn_.sendFrame(makeCall(code, data));
  if (s != Status::Ok) return s;
  Result<Parcel> frame = conn_.recvFrame();
  if (!frame) return frame.status();
  return parseReply(frame.take());
}

}  // namespace mini

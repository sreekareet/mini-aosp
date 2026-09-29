// IRemote: "something I can send a transaction to". Client-side proxies talk only
// to this interface, so they don't care whether it is a socket, a test double, or
// (later) an async connection. Real AOSP: IBinder::transact.
#pragma once

#include <memory>
#include <string>

#include "common/result.h"
#include "ipc/parcel.h"
#include "ipc/transport.h"

namespace mini {

class IRemote {
 public:
  virtual ~IRemote() = default;
  // Sends `data` as method `code` and waits for the reply. Errors from the callee
  // (e.g. NotFound) and transport errors (DeadObject) both come back as the Status.
  virtual Result<Parcel> transact(uint32_t code, const Parcel& data) = 0;
};

// Blocking, single-threaded remote over one connection.
class SocketRemote final : public IRemote {
 public:
  explicit SocketRemote(Connection conn) : conn_(std::move(conn)) {}
  static Result<std::unique_ptr<SocketRemote>> connect(const std::string& path);

  Result<Parcel> transact(uint32_t code, const Parcel& data) override;

 private:
  Connection conn_;
};

}  // namespace mini

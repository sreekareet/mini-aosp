// IRemote: "something I can send a transaction to". Client-side proxies talk only
// to this interface, so they don't care whether it is a socket, a test double, or
// (later) an async connection. Real AOSP: IBinder::transact.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "common/result.h"
#include "ipc/parcel.h"
#include "ipc/transport.h"

namespace mini {

// Server-pushed message (subscription updates). Runs on the connection's loop thread.
using EventHandler = std::function<void(uint32_t code, Parcel& args)>;

// Called (once) when the process on the other end is gone. Runs on the connection's
// loop thread: keep it short, and do not make synchronous calls from it.
using DeathRecipient = std::function<void()>;

class IRemote {
 public:
  virtual ~IRemote() = default;
  // Sends `data` as method `code` and waits for the reply. Errors from the callee
  // (e.g. NotFound) and transport errors (DeadObject) both come back as the Status.
  virtual Result<Parcel> transact(uint32_t code, const Parcel& data) = 0;

  // Receive events pushed by the server. Only remotes with a background reader
  // (RpcClient) can; a plain blocking SocketRemote has nowhere to deliver them.
  virtual Status setEventHandler(EventHandler /*handler*/) { return Status::NotSupported; }

  // Ask to be told when the remote dies. DeadObject if it is already dead.
  // Real AOSP: IBinder::linkToDeath + DeathRecipient::binderDied.
  virtual Status linkToDeath(DeathRecipient /*recipient*/) { return Status::NotSupported; }
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

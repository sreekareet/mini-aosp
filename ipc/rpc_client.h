// RpcClient: a connection that can both make synchronous calls and receive events
// pushed by the server. A background EventLoop thread reads the socket and sorts
// incoming frames: Reply -> wakes the waiting caller, Event -> handler callback.
// Real AOSP: the process's binder thread pool serves both roles.
#pragma once

#include <memory>
#include <string>

#include "common/event_loop.h"
#include "ipc/remote.h"

namespace mini {

// Threading:
//   * transact() may be called from any thread except the loop thread; concurrent
//     callers are serialised (one call in flight per connection).
//   * The event handler runs on the loop thread. It must not call transact() on the
//     same client (that would wait for a reply only the blocked loop could read), so
//     transact() returns IllegalState there instead of deadlocking.
// The EventLoop must outlive the client.
class RpcClient final : public IRemote {
 public:
  static Result<std::unique_ptr<RpcClient>> connect(EventLoop& loop, const std::string& path);
  ~RpcClient() override;

  Result<Parcel> transact(uint32_t code, const Parcel& data) override;
  Status setEventHandler(EventHandler handler) override;

 private:
  struct State;  // shared with the loop callback, which holds only a weak_ptr to it
  RpcClient(EventLoop& loop, std::shared_ptr<State> state) : loop_(loop), state_(std::move(state)) {}

  EventLoop& loop_;
  std::shared_ptr<State> state_;
};

}  // namespace mini

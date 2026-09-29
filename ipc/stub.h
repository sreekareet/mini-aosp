// IStub: the server side of a transaction, the counterpart of IRemote.
// Real AOSP: BBinder::onTransact (generated Bn* classes override it).
#pragma once

#include <sys/types.h>

#include <cstdint>
#include <functional>

#include "common/result.h"
#include "ipc/parcel.h"

namespace mini {

using ConnectionId = uint64_t;

// Who is calling, and how to talk back to them.
struct CallerInfo {
  ConnectionId connection = 0;
  // Kernel-verified identity of the calling process (SO_PEERCRED), the counterpart of
  // Binder.getCallingUid()/getCallingPid(). Stubs use it for permission checks.
  uid_t uid = static_cast<uid_t>(-1);
  pid_t pid = -1;
  // Push an unsolicited Event frame to this caller's connection (how subscriptions
  // deliver updates). Only call it from the server's loop thread. May be empty
  // (e.g. in tests that don't need events) - check before using.
  std::function<Status(uint32_t code, const Parcel& args)> sendEvent;
};

class IStub {
 public:
  virtual ~IStub() = default;
  // Decode `data` (advancing its cursor), do the work, encode results into `reply`.
  // Return UnknownTransaction for a code you don't handle, BadParcel if `data` is short.
  virtual Status onTransact(uint32_t code, Parcel& data, Parcel& reply,
                            const CallerInfo& caller) = 0;

  // The connection closed (client exited or crashed): drop anything held for it.
  virtual void onClientGone(ConnectionId /*connection*/) {}
};

}  // namespace mini

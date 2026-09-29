// IStub: the server side of a transaction, the counterpart of IRemote.
// Real AOSP: BBinder::onTransact (generated Bn* classes override it).
#pragma once

#include <cstdint>

#include "common/result.h"
#include "ipc/parcel.h"

namespace mini {

using ConnectionId = uint64_t;

// Who is calling. Grows in later stages (stage 5 adds uid/pid).
struct CallerInfo {
  ConnectionId connection = 0;
};

class IStub {
 public:
  virtual ~IStub() = default;
  // Decode `data` (advancing its cursor), do the work, encode results into `reply`.
  // Return UnknownTransaction for a code you don't handle, BadParcel if `data` is short.
  virtual Status onTransact(uint32_t code, Parcel& data, Parcel& reply,
                            const CallerInfo& caller) = 0;
};

}  // namespace mini

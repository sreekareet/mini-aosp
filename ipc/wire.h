// The RPC message format, on top of Parcel frames.
//
//   Call  frame: [int32 Call ][uint32 code][arguments...]
//   Reply frame: [int32 Reply][int32 status][results...]     (results only if status is Ok)
//   Event frame: [int32 Event][uint32 code][arguments...]    (server -> client, unsolicited, no reply)
//
// Real Binder does this in the kernel driver plus IPCThreadState/BpBinder; here it
// is a few functions so the whole protocol is readable in one screen.
#pragma once

#include <cstdint>

#include "common/result.h"
#include "ipc/parcel.h"

namespace mini {

enum class FrameType : int32_t { Call = 0, Reply = 1, Event = 2 };

Parcel makeCall(uint32_t code, const Parcel& args);
Parcel makeReply(Status status, const Parcel& results);
Parcel makeEvent(uint32_t code, const Parcel& args);

Result<FrameType> peekFrameType(const Parcel& frame);  // does not move the read cursor

// Decoders leave the Parcel's read cursor at the start of the arguments/results,
// so the same Parcel can be handed on without copying.
Result<uint32_t> parseCall(Parcel& frame);
Result<uint32_t> parseEvent(Parcel& frame);
Result<Parcel> parseReply(Parcel frame);  // error Status from the callee comes back as the error

}  // namespace mini

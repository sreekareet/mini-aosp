#include "ipc/wire.h"

namespace mini {

Parcel makeCall(uint32_t code, const Parcel& args) {
  Parcel p;
  p.writeInt32(static_cast<int32_t>(FrameType::Call));
  p.writeInt32(static_cast<int32_t>(code));
  p.append(args);
  return p;
}

Parcel makeReply(Status status, const Parcel& results) {
  Parcel p;
  p.writeInt32(static_cast<int32_t>(FrameType::Reply));
  p.writeInt32(static_cast<int32_t>(status));
  if (status == Status::Ok) p.append(results);
  return p;
}

Result<uint32_t> parseCall(Parcel& frame) {
  int32_t type = frame.readInt32();
  int32_t code = frame.readInt32();
  if (!frame.ok() || type != static_cast<int32_t>(FrameType::Call)) return Status::BadParcel;
  return static_cast<uint32_t>(code);
}

Result<Parcel> parseReply(Parcel frame) {
  int32_t type = frame.readInt32();
  int32_t status = frame.readInt32();
  if (!frame.ok() || type != static_cast<int32_t>(FrameType::Reply)) return Status::BadParcel;
  if (status != static_cast<int32_t>(Status::Ok)) {
    // The callee reports its own error; pass it through unchanged.
    return static_cast<Status>(status);
  }
  return frame;
}

}  // namespace mini

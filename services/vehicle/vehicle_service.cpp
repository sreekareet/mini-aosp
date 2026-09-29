#include "services/vehicle/vehicle_service.h"

namespace mini {

Status VehicleStub::onTransact(uint32_t code, Parcel& data, Parcel& reply,
                               const CallerInfo& /*caller*/) {
  switch (code) {
    case kGetProperty: {
      auto id = static_cast<PropertyId>(data.readInt32());
      if (!data.ok()) return Status::BadParcel;
      Result<PropValue> v = impl_.getProperty(id);
      if (!v) return v.status();
      writeValue(reply, v.value());
      return Status::Ok;
    }
    case kSetProperty: {
      auto id = static_cast<PropertyId>(data.readInt32());
      PropValue value = readValue(data);
      if (!data.ok()) return Status::BadParcel;
      return impl_.setProperty(id, value);
    }
    case kListProperties: {
      Result<std::vector<PropertyConfig>> list = impl_.listProperties();
      if (!list) return list.status();
      reply.writeInt32(static_cast<int32_t>(list.value().size()));
      for (const PropertyConfig& c : list.value()) {
        reply.writeInt32(static_cast<int32_t>(c.id));
        reply.writeInt32(static_cast<int32_t>(c.type));
        reply.writeBool(c.writable);
      }
      return Status::Ok;
    }
  }
  return Status::UnknownTransaction;
}

}  // namespace mini

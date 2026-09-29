#include "services/vehicle/vehicle_iface.h"

namespace mini {

void writeValue(Parcel& p, const PropValue& v) {
  p.writeInt32(static_cast<int32_t>(typeOf(v)));
  if (const bool* b = std::get_if<bool>(&v)) p.writeBool(*b);
  else if (const int32_t* i = std::get_if<int32_t>(&v)) p.writeInt32(*i);
  else p.writeFloat(std::get<float>(v));
}

PropValue readValue(Parcel& p) {
  switch (static_cast<ValueType>(p.readInt32())) {
    case ValueType::Bool: return PropValue{p.readBool()};
    case ValueType::Int32: return PropValue{p.readInt32()};
    case ValueType::Float: return PropValue{p.readFloat()};
  }
  p.markBad();  // unknown tag from the wire
  return PropValue{false};
}

Result<PropValue> VehicleServiceProxy::getProperty(PropertyId id) {
  Parcel data;
  data.writeInt32(static_cast<int32_t>(id));
  Result<Parcel> r = remote_->transact(kGetProperty, data);
  if (!r) return r.status();
  PropValue v = readValue(r.value());
  if (!r.value().ok()) return Status::BadParcel;
  return v;
}

Status VehicleServiceProxy::setProperty(PropertyId id, const PropValue& value) {
  Parcel data;
  data.writeInt32(static_cast<int32_t>(id));
  writeValue(data, value);
  return remote_->transact(kSetProperty, data).status();
}

Result<std::vector<PropertyConfig>> VehicleServiceProxy::listProperties() {
  Result<Parcel> r = remote_->transact(kListProperties, Parcel());
  if (!r) return r.status();
  Parcel& p = r.value();
  int32_t n = p.readInt32();
  std::vector<PropertyConfig> out;
  for (int32_t i = 0; i < n && p.ok(); ++i) {
    PropertyConfig c;
    c.id = static_cast<PropertyId>(p.readInt32());
    c.type = static_cast<ValueType>(p.readInt32());
    c.writable = p.readBool();
    out.push_back(c);
  }
  if (!p.ok()) return Status::BadParcel;
  return out;
}

}  // namespace mini

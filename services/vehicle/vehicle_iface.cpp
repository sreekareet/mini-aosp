#include "services/vehicle/vehicle_iface.h"

#include <algorithm>
#include <map>

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

// Which listeners care about which property. Written by callers, read by the loop
// thread when an event arrives, hence the mutex.
struct VehicleServiceProxy::ListenerTable {
  using Listeners = std::vector<std::shared_ptr<IPropertyListener>>;

  // Returns true if this was the first listener for `id`.
  bool add(PropertyId id, std::shared_ptr<IPropertyListener> l) {
    std::lock_guard<std::mutex> lock(mu);
    Listeners& v = byProperty[id];
    v.push_back(std::move(l));
    return v.size() == 1;
  }

  // Returns: NotFound if it wasn't registered; otherwise sets `wasLast`.
  Status remove(PropertyId id, const std::shared_ptr<IPropertyListener>& l, bool& wasLast) {
    std::lock_guard<std::mutex> lock(mu);
    auto it = byProperty.find(id);
    if (it == byProperty.end()) return Status::NotFound;
    Listeners& v = it->second;
    auto pos = std::find(v.begin(), v.end(), l);
    if (pos == v.end()) return Status::NotFound;
    v.erase(pos);
    wasLast = v.empty();
    if (wasLast) byProperty.erase(it);
    return Status::Ok;
  }

  void dispatch(PropertyId id, const PropValue& value) {
    Listeners copy;
    {
      std::lock_guard<std::mutex> lock(mu);
      auto it = byProperty.find(id);
      if (it == byProperty.end()) return;
      copy = it->second;  // call listeners without the lock: they may (un)subscribe
    }
    for (auto& l : copy) l->onPropertyChanged(id, value);
  }

  std::mutex mu;
  std::map<PropertyId, Listeners> byProperty;
};

VehicleServiceProxy::VehicleServiceProxy(std::unique_ptr<IRemote> remote)
    : remote_(std::move(remote)), table_(std::make_shared<ListenerTable>()) {}

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

Status VehicleServiceProxy::subscribe(PropertyId id, std::shared_ptr<IPropertyListener> listener) {
  if (!listener) return Status::InvalidArgument;
  std::lock_guard<std::mutex> lock(subscribeMutex_);

  if (!handlerInstalled_) {
    // The handler owns a share of the table, so it stays valid even if this proxy dies first.
    std::shared_ptr<ListenerTable> table = table_;
    Status s = remote_->setEventHandler([table](uint32_t code, Parcel& args) {
      if (code != kOnPropertyChanged) return;
      auto id = static_cast<PropertyId>(args.readInt32());
      PropValue value = readValue(args);
      if (args.ok()) table->dispatch(id, value);
    });
    if (s != Status::Ok) return s;
    handlerInstalled_ = true;
  }

  bool first = table_->add(id, listener);
  if (!first) return Status::Ok;  // the server already sends us this property

  Parcel data;
  data.writeInt32(static_cast<int32_t>(id));
  Status s = remote_->transact(kSubscribe, data).status();
  if (s != Status::Ok) {
    bool wasLast = false;
    table_->remove(id, listener, wasLast);  // roll back
  }
  return s;
}

Status VehicleServiceProxy::unsubscribe(PropertyId id,
                                        const std::shared_ptr<IPropertyListener>& listener) {
  std::lock_guard<std::mutex> lock(subscribeMutex_);
  bool wasLast = false;
  Status s = table_->remove(id, listener, wasLast);
  if (s != Status::Ok || !wasLast) return s;

  Parcel data;
  data.writeInt32(static_cast<int32_t>(id));
  return remote_->transact(kUnsubscribe, data).status();
}

}  // namespace mini

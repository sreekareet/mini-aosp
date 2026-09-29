#include "services/vehicle/vehicle_service.h"

#include <algorithm>

namespace mini {

VehicleService::VehicleService(IVehicleHal& hal) : hal_(hal) {
  hal_.setChangeCallback([this](PropertyId id, const PropValue& v) { onHalChange(id, v); });
}

// The HAL outlives us, so it must not keep a callback pointing at a dead object.
VehicleService::~VehicleService() { hal_.setChangeCallback(nullptr); }

Status VehicleService::subscribe(PropertyId id, std::shared_ptr<IPropertyListener> listener) {
  if (!listener) return Status::InvalidArgument;
  if (!hal_.get(id)) return Status::NotFound;  // can't subscribe to what the HAL doesn't have
  auto& v = listeners_[id];
  if (std::find(v.begin(), v.end(), listener) == v.end()) v.push_back(std::move(listener));
  return Status::Ok;
}

Status VehicleService::unsubscribe(PropertyId id,
                                   const std::shared_ptr<IPropertyListener>& listener) {
  auto it = listeners_.find(id);
  if (it == listeners_.end()) return Status::NotFound;
  auto& v = it->second;
  auto pos = std::find(v.begin(), v.end(), listener);
  if (pos == v.end()) return Status::NotFound;
  v.erase(pos);
  if (v.empty()) listeners_.erase(it);
  return Status::Ok;
}

void VehicleService::onHalChange(PropertyId id, const PropValue& value) {
  auto it = listeners_.find(id);
  if (it == listeners_.end()) return;
  auto copy = it->second;  // a listener may unsubscribe while being notified
  for (auto& l : copy) l->onPropertyChanged(id, value);
}

// Turns "property changed" into an Event frame on one client's connection.
class VehicleStub::RemoteListener final : public IPropertyListener {
 public:
  explicit RemoteListener(std::function<Status(uint32_t, const Parcel&)> send)
      : send_(std::move(send)) {}

  void onPropertyChanged(PropertyId id, const PropValue& value) override {
    Parcel args;
    args.writeInt32(static_cast<int32_t>(id));
    writeValue(args, value);
    send_(kOnPropertyChanged, args);  // failure = client gone; onClientGone cleans up
  }

 private:
  std::function<Status(uint32_t, const Parcel&)> send_;
};

Status VehicleStub::onTransact(uint32_t code, Parcel& data, Parcel& reply,
                               const CallerInfo& caller) {
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
    case kSubscribe: {
      auto id = static_cast<PropertyId>(data.readInt32());
      if (!data.ok()) return Status::BadParcel;
      if (!caller.sendEvent) return Status::NotSupported;  // caller can't receive events
      ClientState& client = clients_[caller.connection];
      if (!client.listener) client.listener = std::make_shared<RemoteListener>(caller.sendEvent);
      Status s = impl_.subscribe(id, client.listener);
      if (s == Status::Ok) client.subscribed.insert(id);
      return s;
    }
    case kUnsubscribe: {
      auto id = static_cast<PropertyId>(data.readInt32());
      if (!data.ok()) return Status::BadParcel;
      auto it = clients_.find(caller.connection);
      if (it == clients_.end()) return Status::NotFound;
      it->second.subscribed.erase(id);
      return impl_.unsubscribe(id, it->second.listener);
    }
  }
  return Status::UnknownTransaction;
}

// Without this a crashed client would leave its listener registered forever, and
// the service would keep trying to write to a closed socket.
void VehicleStub::onClientGone(ConnectionId connection) {
  auto it = clients_.find(connection);
  if (it == clients_.end()) return;
  for (PropertyId id : it->second.subscribed) impl_.unsubscribe(id, it->second.listener);
  clients_.erase(it);
}

}  // namespace mini

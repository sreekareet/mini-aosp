// ServiceManager: the name -> address registry. Services register under a name;
// clients look the name up, then talk to the service directly.
// Real AOSP: servicemanager hands out Binder handles. We hand out socket paths,
// which is the simplest thing that plays the same role.
#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ipc/remote.h"
#include "ipc/stub.h"

namespace mini {

enum ServiceManagerTx : uint32_t {
  kAddService = 1,    // (string name, string socketPath) -> ()
  kGetService = 2,    // (string name) -> (string socketPath)
  kListServices = 3,  // () -> (int32 n, string name * n)
};

// Server side (a "Bn" stub): holds the registry.
class ServiceManager final : public IStub {
 public:
  Status onTransact(uint32_t code, Parcel& data, Parcel& reply, const CallerInfo& caller) override;

 private:
  std::map<std::string, std::string> services_;  // name -> socket path
};

// Client side (a "Bp" proxy): same methods, but each one is a transaction.
class ServiceManagerProxy {
 public:
  explicit ServiceManagerProxy(std::unique_ptr<IRemote> remote) : remote_(std::move(remote)) {}

  Status addService(const std::string& name, const std::string& socketPath);
  Result<std::string> getService(const std::string& name);
  Result<std::vector<std::string>> listServices();

 private:
  std::unique_ptr<IRemote> remote_;
};

// Connect to the ServiceManager, retrying for up to `wait` (it may still be
// starting - real AOSP has waitForService for the same reason).
Result<std::unique_ptr<SocketRemote>> connectToServiceManager(
    std::chrono::milliseconds wait = std::chrono::milliseconds(0));

// Register `name` at `socketPath` with the ServiceManager.
Status registerService(const std::string& name, const std::string& socketPath,
                       std::chrono::milliseconds wait = std::chrono::milliseconds(0));

// Look `name` up and connect to it. NotFound if nobody registered it.
Result<std::unique_ptr<SocketRemote>> getServiceRemote(const std::string& name);

}  // namespace mini

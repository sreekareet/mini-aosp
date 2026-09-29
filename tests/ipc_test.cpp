#include <gtest/gtest.h>
#include <sys/socket.h>

#include <atomic>

#include "common/paths.h"
#include "ipc/rpc_client.h"
#include "ipc/service_manager.h"
#include "test_util.h"

using namespace mini;

TEST(ParcelTest, RoundTrip) {
  Parcel p;
  p.writeInt32(-7);
  p.writeBool(true);
  p.writeFloat(1.5f);
  p.writeString("hello");
  EXPECT_EQ(p.readInt32(), -7);
  EXPECT_TRUE(p.readBool());
  EXPECT_FLOAT_EQ(p.readFloat(), 1.5f);
  EXPECT_EQ(p.readString(), "hello");
  EXPECT_TRUE(p.ok());
  EXPECT_EQ(p.remaining(), 0u);
}

TEST(ParcelTest, ReadingPastTheEndIsStickyFailure) {
  Parcel p;
  p.writeInt32(1);
  EXPECT_EQ(p.readInt32(), 1);
  EXPECT_EQ(p.readInt32(), 0);  // nothing left: zero value, and now bad
  EXPECT_FALSE(p.ok());
  EXPECT_EQ(p.readString(), "");
  EXPECT_FALSE(p.ok());
}

TEST(ParcelTest, LyingStringLengthIsRejected) {
  Parcel p;
  p.writeInt32(1000);  // claims 1000 bytes...
  p.writeInt32(0);     // ...but only 4 follow
  EXPECT_EQ(p.readString(), "");
  EXPECT_FALSE(p.ok());
}

namespace {
struct SocketPair {
  SocketPair() {
    int fds[2];
    EXPECT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds), 0);
    a = std::make_unique<Connection>(UniqueFd(fds[0]));
    b = std::make_unique<Connection>(UniqueFd(fds[1]));
  }
  std::unique_ptr<Connection> a, b;
};
}  // namespace

TEST(ConnectionTest, FramesKeepTheirBoundaries) {
  SocketPair sp;
  Parcel one, two;
  one.writeString("first");
  two.writeString("second");
  ASSERT_EQ(sp.a->sendFrame(one), Status::Ok);
  ASSERT_EQ(sp.a->sendFrame(two), Status::Ok);  // both may arrive in a single read
  Result<Parcel> r1 = sp.b->recvFrame();
  Result<Parcel> r2 = sp.b->recvFrame();
  ASSERT_TRUE(r1.ok() && r2.ok());
  EXPECT_EQ(r1.value().readString(), "first");
  EXPECT_EQ(r2.value().readString(), "second");
}

TEST(ConnectionTest, OversizedFrameHeaderIsRejected) {
  SocketPair sp;
  uint32_t huge = static_cast<uint32_t>(kMaxFrameSize) + 1;
  ASSERT_EQ(::send(sp.a->fd(), &huge, 4, 0), 4);
  EXPECT_EQ(sp.b->recvFrame().status(), Status::BadParcel);
}

TEST(ConnectionTest, ClosedPeerIsDeadObject) {
  SocketPair sp;
  sp.a.reset();
  EXPECT_EQ(sp.b->recvFrame().status(), Status::DeadObject);
}

TEST(ConnectionTest, ConnectToMissingPath) {
  EXPECT_EQ(Connection::connectTo("/tmp/mini-aosp-no-such.sock").status(), Status::NotFound);
}

TEST(ServiceManagerTest, AddGetList) {
  ServiceManager sm;
  ServiceManagerProxy proxy(std::make_unique<test::LoopbackRemote>(sm));
  ASSERT_EQ(proxy.addService("vehicle", "/run/vehicle.sock"), Status::Ok);
  auto path = proxy.getService("vehicle");
  ASSERT_TRUE(path.ok());
  EXPECT_EQ(path.value(), "/run/vehicle.sock");
  auto names = proxy.listServices();
  ASSERT_TRUE(names.ok());
  EXPECT_EQ(names.value(), std::vector<std::string>{"vehicle"});
}

TEST(ServiceManagerTest, MissingAndInvalid) {
  ServiceManager sm;
  ServiceManagerProxy proxy(std::make_unique<test::LoopbackRemote>(sm));
  EXPECT_EQ(proxy.getService("nope").status(), Status::NotFound);
  EXPECT_EQ(proxy.addService("bad name!", "/x"), Status::InvalidArgument);
  EXPECT_EQ(proxy.addService("", "/x"), Status::InvalidArgument);
}

TEST(ServiceManagerTest, UnknownTransaction) {
  ServiceManager sm;
  test::LoopbackRemote remote(sm);
  EXPECT_EQ(remote.transact(999, Parcel()).status(), Status::UnknownTransaction);
}

TEST(ServiceManagerTest, OnlyAllowedUidsMayRegister) {
  ServiceManager sm(UidAllowList::only({1000}));
  auto remote = std::make_unique<test::LoopbackRemote>(sm);
  test::LoopbackRemote* raw = remote.get();
  ServiceManagerProxy proxy(std::move(remote));

  raw->setCaller(2000);  // an unrelated user tries to take over a name
  EXPECT_EQ(proxy.addService("vehicle", "/tmp/evil.sock"), Status::PermissionDenied);
  EXPECT_EQ(proxy.getService("vehicle").status(), Status::NotFound);  // nothing was registered
  raw->setCaller(1000);
  EXPECT_EQ(proxy.addService("vehicle", "/run/vehicle.sock"), Status::Ok);
  raw->setCaller(2000);
  EXPECT_TRUE(proxy.getService("vehicle").ok());  // lookups are open to everyone
  raw->setCaller(0);                              // root always passes
  EXPECT_EQ(proxy.addService("other", "/run/other.sock"), Status::Ok);
}

TEST(AccessPolicyTest, DefaultAllowsOnlyRoot) {
  UidAllowList none;  // forgetting to configure it fails closed
  EXPECT_TRUE(none.allows(0));
  EXPECT_FALSE(none.allows(1000));
  EXPECT_FALSE(none.allows(static_cast<uid_t>(-1)));  // unknown identity
  EXPECT_TRUE(UidAllowList::anyone().allows(1000));
}

TEST(ConnectionTest, PeerCredentialsComeFromTheKernel) {
  SocketPair sp;
  auto cred = sp.a->peerCredentials();
  ASSERT_TRUE(cred.ok());
  EXPECT_EQ(cred.value().uid, ::geteuid());  // both ends are this process
  EXPECT_EQ(cred.value().pid, ::getpid());
}

// Death notification against a real process.
TEST(RpcClientTest, DeathRecipientFiresWhenTheServerProcessDies) {
  test::TempRunDir run;
  test::ChildProcess sm(std::string(BIN_DIR) + "/servicemanager");
  ASSERT_TRUE(test::waitFor([&] { return test::fileExists(serviceManagerSocketPath()); }));

  LoopThread lt;
  auto client = RpcClient::connect(lt.loop(), serviceManagerSocketPath());
  ASSERT_TRUE(client.ok());

  std::atomic<int> deaths{0};
  ASSERT_EQ(client.value()->linkToDeath([&] { ++deaths; }), Status::Ok);
  EXPECT_TRUE(client.value()->transact(kListServices, Parcel()).ok());  // alive and well
  EXPECT_EQ(deaths, 0);

  sm.stop(SIGKILL);
  ASSERT_TRUE(test::waitFor([&] { return deaths.load() == 1; }));
  EXPECT_EQ(client.value()->transact(kListServices, Parcel()).status(), Status::DeadObject);
  EXPECT_EQ(client.value()->linkToDeath([] {}), Status::DeadObject);  // too late to link
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_EQ(deaths, 1);  // fired exactly once
}

TEST(RpcClientTest, SyncRemoteCannotLinkToDeath) {
  SocketPair sp;
  SocketRemote remote(std::move(*sp.a));
  EXPECT_EQ(remote.linkToDeath([] {}), Status::NotSupported);
}

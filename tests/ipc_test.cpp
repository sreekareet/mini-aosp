#include <gtest/gtest.h>
#include <sys/socket.h>

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

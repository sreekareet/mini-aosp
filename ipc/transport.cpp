#include "ipc/transport.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <cerrno>
#include <cstring>

#include "common/log.h"

namespace mini {

namespace {
constexpr const char* kTag = "Transport";

bool makeAddr(const std::string& path, sockaddr_un& addr) {
  if (path.empty() || path.size() >= sizeof(addr.sun_path)) return false;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  std::memcpy(addr.sun_path, path.c_str(), path.size());
  return true;
}
}  // namespace

Result<Connection> Connection::connectTo(const std::string& path) {
  sockaddr_un addr;
  if (!makeAddr(path, addr)) return Status::InvalidArgument;
  UniqueFd fd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
  if (!fd.valid()) return Status::IoError;
  if (::connect(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    if (errno == ENOENT) return Status::NotFound;
    if (errno == ECONNREFUSED) return Status::DeadObject;
    return Status::IoError;
  }
  return Connection(std::move(fd));
}

Status Connection::setNonBlocking() {
  int flags = ::fcntl(fd_.get(), F_GETFL, 0);
  if (flags < 0 || ::fcntl(fd_.get(), F_SETFL, flags | O_NONBLOCK) < 0) return Status::IoError;
  return Status::Ok;
}

Result<PeerCredentials> Connection::peerCredentials() const {
  ucred cred = {};
  socklen_t len = sizeof(cred);
  if (::getsockopt(fd_.get(), SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0) return Status::IoError;
  PeerCredentials out;
  out.pid = cred.pid;
  out.uid = cred.uid;
  out.gid = cred.gid;
  return out;
}

Status Connection::sendFrame(const Parcel& payload) {
  if (payload.size() > kMaxFrameSize) return Status::InvalidArgument;
  // Header and payload go out in one buffer so frames from one sender never interleave.
  std::vector<uint8_t> out(4 + payload.size());
  uint32_t len = static_cast<uint32_t>(payload.size());
  std::memcpy(out.data(), &len, 4);
  std::memcpy(out.data() + 4, payload.bytes().data(), payload.size());

  size_t sent = 0;
  while (sent < out.size()) {
    // MSG_NOSIGNAL: a dead peer gives EPIPE, not a process-killing SIGPIPE.
    ssize_t n = ::send(fd_.get(), out.data() + sent, out.size() - sent, MSG_NOSIGNAL);
    if (n > 0) {
      sent += static_cast<size_t>(n);
    } else if (n < 0 && errno == EINTR) {
      continue;
    } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      pollfd p{fd_.get(), POLLOUT, 0};  // peer is slow; give it a second, then give up
      if (::poll(&p, 1, 1000) <= 0) return Status::IoError;
    } else {
      return (errno == EPIPE || errno == ECONNRESET) ? Status::DeadObject : Status::IoError;
    }
  }
  return Status::Ok;
}

Status Connection::fill() {
  uint8_t chunk[4096];
  for (;;) {
    ssize_t n = ::recv(fd_.get(), chunk, sizeof(chunk), 0);
    if (n > 0) {
      in_.insert(in_.end(), chunk, chunk + n);
      return Status::Ok;
    }
    if (n == 0) return Status::DeadObject;  // orderly shutdown by the peer
    if (errno == EINTR) continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return Status::WouldBlock;
    return errno == ECONNRESET ? Status::DeadObject : Status::IoError;
  }
}

Result<Parcel> Connection::takeFrame() {
  if (in_.size() < 4) return Status::WouldBlock;
  uint32_t len = 0;
  std::memcpy(&len, in_.data(), 4);
  if (len > kMaxFrameSize) return Status::BadParcel;
  if (in_.size() < 4 + static_cast<size_t>(len)) return Status::WouldBlock;
  std::vector<uint8_t> payload(in_.begin() + 4, in_.begin() + 4 + len);
  in_.erase(in_.begin(), in_.begin() + 4 + len);
  return Parcel::fromBytes(std::move(payload));
}

Result<Parcel> Connection::recvFrame() {
  for (;;) {
    Result<Parcel> frame = takeFrame();
    if (frame.status() != Status::WouldBlock) return frame;
    Status s = fill();
    if (s != Status::Ok && s != Status::WouldBlock) return s;
    if (s == Status::WouldBlock) {  // only possible on a non-blocking fd: wait for data
      pollfd p{fd_.get(), POLLIN, 0};
      ::poll(&p, 1, -1);
    }
  }
}

Result<Listener> Listener::bind(const std::string& path) {
  sockaddr_un addr;
  if (!makeAddr(path, addr)) return Status::InvalidArgument;

  std::string dir = path.substr(0, path.find_last_of('/'));
  if (!dir.empty() && ::mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) return Status::IoError;

  UniqueFd fd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
  if (!fd.valid()) return Status::IoError;
  ::unlink(path.c_str());  // a crashed predecessor leaves its socket file behind
  if (::bind(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    logError(kTag, "bind(%s): %s", path.c_str(), std::strerror(errno));
    return Status::IoError;
  }
  // World-connectable on purpose: like Binder's device node, access control is
  // done by the service checking the caller's uid (stage 5), not by file modes.
  ::chmod(path.c_str(), 0666);
  if (::listen(fd.get(), 16) != 0) {
    ::unlink(path.c_str());
    return Status::IoError;
  }
  return Listener(std::move(fd), path);
}

Listener::Listener(Listener&& other) noexcept
    : fd_(std::move(other.fd_)), path_(std::exchange(other.path_, {})) {}

Listener::~Listener() {
  if (!path_.empty()) ::unlink(path_.c_str());
}

Result<Connection> Listener::accept() {
  int c = ::accept4(fd_.get(), nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
  if (c < 0) return (errno == EAGAIN || errno == EWOULDBLOCK) ? Status::WouldBlock : Status::IoError;
  return Connection(UniqueFd(c));
}

}  // namespace mini

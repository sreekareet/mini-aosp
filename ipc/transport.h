// Unix domain socket transport: length-prefixed frames, each frame one Parcel.
#pragma once

#include <string>
#include <vector>

#include "common/result.h"
#include "common/unique_fd.h"
#include "ipc/parcel.h"

namespace mini {

inline constexpr size_t kMaxFrameSize = 1 << 20;  // reject absurd lengths from a bad peer

// One end of a connected stream socket. A stream has no message boundaries, so
// every frame is [uint32 length][payload], and incoming bytes are buffered until
// a whole frame is available. That lets a non-blocking server serve many clients
// without a slow one stalling it mid-frame.
class Connection {
 public:
  explicit Connection(UniqueFd fd) : fd_(std::move(fd)) {}

  // NotFound: no such socket path. DeadObject: path exists but nobody listens
  // (a server that crashed leaves its socket file behind).
  static Result<Connection> connectTo(const std::string& path);

  int fd() const { return fd_.get(); }
  Status setNonBlocking();

  Status sendFrame(const Parcel& payload);

  // Non-blocking building blocks (used by the server):
  Status fill();                // one recv(): Ok, WouldBlock, DeadObject (EOF), IoError
  Result<Parcel> takeFrame();   // a buffered frame, or WouldBlock if incomplete, BadParcel if oversize

  // Blocking convenience (used by clients): wait until a full frame arrives.
  Result<Parcel> recvFrame();

 private:
  UniqueFd fd_;
  std::vector<uint8_t> in_;
};

// A listening Unix socket bound to a filesystem path.
class Listener {
 public:
  // Creates the parent directory if needed and replaces a stale socket file.
  static Result<Listener> bind(const std::string& path);

  Listener(Listener&& other) noexcept;
  Listener& operator=(Listener&&) = delete;
  Listener(const Listener&) = delete;
  Listener& operator=(const Listener&) = delete;
  ~Listener();  // unlinks the socket file (RAII for the path, not just the fd)

  int fd() const { return fd_.get(); }
  const std::string& path() const { return path_; }

  // Returns a non-blocking Connection, or WouldBlock if none is pending.
  Result<Connection> accept();

 private:
  Listener(UniqueFd fd, std::string path) : fd_(std::move(fd)), path_(std::move(path)) {}
  UniqueFd fd_;
  std::string path_;
};

}  // namespace mini

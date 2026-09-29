#include "ipc/rpc_client.h"

#include <sys/socket.h>

#include <condition_variable>
#include <mutex>
#include <optional>

#include "ipc/wire.h"

namespace mini {

namespace {
constexpr auto kCallTimeout = std::chrono::seconds(5);
}

// Everything the loop thread and callers share, behind `mu`. `conn` is the exception:
// only the loop thread reads from it, only the call-holder writes to it (a socket
// can be read and written concurrently), so its buffer is never shared.
struct RpcClient::State {
  State(Connection c, EventLoop& l) : conn(std::move(c)), loop(l), fd(conn.fd()) {}

  Connection conn;
  EventLoop& loop;
  const int fd;

  std::mutex callMutex;  // serialises callers
  std::mutex mu;         // guards the fields below
  std::condition_variable cv;
  std::optional<Parcel> reply;
  bool dead = false;
  EventHandler handler;
  DeathRecipient deathRecipient;

  void onReadable();
  void markDead();
};

void RpcClient::State::markDead() {
  DeathRecipient recipient;
  {
    std::lock_guard<std::mutex> lock(mu);
    if (dead) return;
    dead = true;
    recipient = std::move(deathRecipient);  // fires at most once
  }
  cv.notify_all();  // wake a caller blocked in transact(): it will see DeadObject
  loop.removeFd(fd);
  if (recipient) recipient();  // outside the lock: the recipient may take its own locks
}

void RpcClient::State::onReadable() {
  Status s = conn.fill();
  for (;;) {
    Result<Parcel> frame = conn.takeFrame();
    if (frame.status() == Status::WouldBlock) break;
    if (!frame) {
      s = frame.status();
      break;
    }
    Parcel p = frame.take();
    Result<FrameType> type = peekFrameType(p);
    if (type && type.value() == FrameType::Reply) {
      {
        std::lock_guard<std::mutex> lock(mu);
        reply = std::move(p);
      }
      cv.notify_one();
    } else if (type && type.value() == FrameType::Event) {
      Result<uint32_t> code = parseEvent(p);
      EventHandler h;
      {
        std::lock_guard<std::mutex> lock(mu);
        h = handler;  // copy, so we can call it without holding the lock
      }
      if (code && h) h(code.value(), p);
    }  // anything else: ignore the frame
  }
  if (s != Status::Ok && s != Status::WouldBlock) markDead();  // EOF, reset, or garbage
}

Result<std::unique_ptr<RpcClient>> RpcClient::connect(EventLoop& loop, const std::string& path) {
  Result<Connection> conn = Connection::connectTo(path);
  if (!conn) return conn.status();
  Connection c = conn.take();
  Status s = c.setNonBlocking();
  if (s != Status::Ok) return s;

  auto state = std::make_shared<State>(std::move(c), loop);
  // The loop must never keep the State alive or touch it after the client is gone:
  // capture a weak_ptr and skip the callback if the client has been destroyed.
  std::weak_ptr<State> weak = state;
  s = loop.addFd(state->fd, EPOLLIN, [weak](uint32_t) {
    if (std::shared_ptr<State> st = weak.lock()) st->onReadable();
  });
  if (s != Status::Ok) return s;
  return std::unique_ptr<RpcClient>(new RpcClient(loop, std::move(state)));
}

RpcClient::~RpcClient() { loop_.removeFd(state_->fd); }

Status RpcClient::setEventHandler(EventHandler handler) {
  std::lock_guard<std::mutex> lock(state_->mu);
  state_->handler = std::move(handler);
  return Status::Ok;
}

Status RpcClient::linkToDeath(DeathRecipient recipient) {
  std::lock_guard<std::mutex> lock(state_->mu);
  if (state_->dead) return Status::DeadObject;  // too late: report it instead of a callback
  state_->deathRecipient = std::move(recipient);
  return Status::Ok;
}

Result<Parcel> RpcClient::transact(uint32_t code, const Parcel& data) {
  if (loop_.inLoopThread()) return Status::IllegalState;
  State& st = *state_;
  std::lock_guard<std::mutex> oneCall(st.callMutex);

  {
    std::lock_guard<std::mutex> lock(st.mu);
    if (st.dead) return Status::DeadObject;
    st.reply.reset();
  }
  Status s = st.conn.sendFrame(makeCall(code, data));
  if (s != Status::Ok) return s;

  std::unique_lock<std::mutex> lock(st.mu);
  st.cv.wait_for(lock, kCallTimeout, [&] { return st.reply.has_value() || st.dead; });
  if (st.reply) {
    Parcel frame = std::move(*st.reply);
    st.reply.reset();
    lock.unlock();
    return parseReply(std::move(frame));
  }
  if (st.dead) return Status::DeadObject;

  // No reply: the connection is in an unknown state (a late reply could be mistaken
  // for the next call's), so kill it rather than risk mixing them up.
  ::shutdown(st.fd, SHUT_RDWR);
  return Status::Timeout;
}

}  // namespace mini

// EventLoop: one thread waiting (epoll) for file descriptors, posted tasks and
// timers, and running their callbacks one at a time.
// Real AOSP: Looper (epoll + eventfd wake pipe) with Handler/Message on top.
#pragma once

#include <sys/epoll.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "common/result.h"
#include "common/unique_fd.h"

namespace mini {

// Threading model - the important part:
//   * Callbacks (fd callbacks, tasks, timers) all run on the thread that called run().
//     Code that only ever runs there needs no locks (the servers rely on this).
//   * addFd / removeFd / post / postDelayed / quit may be called from any thread.
//   * removeFd does not wait for a callback that is already running. If a callback
//     captures an object, make sure the object outlives the loop's use of it - either
//     stop the loop first, or capture a weak_ptr (RpcClient does).
class EventLoop {
 public:
  using FdCallback = std::function<void(uint32_t events)>;  // events: EPOLLIN, EPOLLHUP, ...
  using Task = std::function<void()>;

  EventLoop();
  ~EventLoop() = default;
  EventLoop(const EventLoop&) = delete;
  EventLoop& operator=(const EventLoop&) = delete;

  Status addFd(int fd, uint32_t events, FdCallback callback);
  void removeFd(int fd);

  void post(Task task);                                        // run soon, on the loop thread
  void postDelayed(std::chrono::milliseconds delay, Task task);  // run after `delay`

  void run();   // blocks until quit()
  void quit();  // makes run() return; safe from any thread, also from a callback

  bool inLoopThread() const { return loopThread_.load() == std::this_thread::get_id(); }

 private:
  using Clock = std::chrono::steady_clock;

  void wake();
  int nextTimeoutMs();
  void runTasks();
  void runTimers();

  UniqueFd epoll_;
  UniqueFd wake_;  // eventfd: writing to it makes epoll_wait return (how other threads poke the loop)

  std::mutex mutex_;  // guards the three containers below
  std::map<int, std::shared_ptr<FdCallback>> fds_;
  std::deque<Task> tasks_;
  std::multimap<Clock::time_point, Task> timers_;

  std::atomic<bool> quit_{false};
  std::atomic<std::thread::id> loopThread_{};
};

// An EventLoop running on its own thread; stops and joins on destruction.
// Real AOSP: HandlerThread.
class LoopThread {
 public:
  LoopThread() : thread_([this] { loop_.run(); }) {}
  ~LoopThread() { stop(); }
  LoopThread(const LoopThread&) = delete;
  LoopThread& operator=(const LoopThread&) = delete;

  EventLoop& loop() { return loop_; }
  void stop() {  // idempotent
    loop_.quit();
    if (thread_.joinable()) thread_.join();
  }

 private:
  EventLoop loop_;      // declared before thread_: the loop exists before its thread starts
  std::thread thread_;
};

}  // namespace mini

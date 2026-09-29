#include "common/event_loop.h"

#include <sys/eventfd.h>

#include <cerrno>
#include <cstdlib>
#include <vector>

#include "common/log.h"

namespace mini {

EventLoop::EventLoop()
    : epoll_(::epoll_create1(EPOLL_CLOEXEC)), wake_(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) {
  epoll_event ev = {};
  ev.events = EPOLLIN;
  ev.data.fd = wake_.get();
  if (!epoll_.valid() || !wake_.valid() ||
      ::epoll_ctl(epoll_.get(), EPOLL_CTL_ADD, wake_.get(), &ev) != 0) {
    // Only happens when the process is out of file descriptors; nothing sensible to do.
    logError("EventLoop", "cannot create epoll/eventfd");
    std::abort();
  }
}

Status EventLoop::addFd(int fd, uint32_t events, FdCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (fds_.count(fd) != 0) return Status::InvalidArgument;
  epoll_event ev = {};
  ev.events = events;
  ev.data.fd = fd;
  if (::epoll_ctl(epoll_.get(), EPOLL_CTL_ADD, fd, &ev) != 0) return Status::IoError;
  fds_[fd] = std::make_shared<FdCallback>(std::move(callback));
  return Status::Ok;
}

void EventLoop::removeFd(int fd) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (fds_.erase(fd) != 0) ::epoll_ctl(epoll_.get(), EPOLL_CTL_DEL, fd, nullptr);
}

void EventLoop::post(Task task) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.push_back(std::move(task));
  }
  wake();
}

void EventLoop::postDelayed(std::chrono::milliseconds delay, Task task) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    timers_.emplace(Clock::now() + delay, std::move(task));
  }
  wake();  // the new timer may be earlier than what the loop is currently waiting for
}

void EventLoop::quit() {
  quit_ = true;
  wake();
}

void EventLoop::wake() {
  uint64_t one = 1;
  ssize_t n = ::write(wake_.get(), &one, sizeof(one));
  (void)n;  // EAGAIN just means a wake-up is already pending, which is all we need
}

int EventLoop::nextTimeoutMs() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!tasks_.empty()) return 0;
  if (timers_.empty()) return -1;  // sleep until something happens
  auto wait = timers_.begin()->first - Clock::now();
  if (wait <= Clock::duration::zero()) return 0;
  return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(wait).count()) + 1;
}

void EventLoop::runTasks() {
  std::deque<Task> batch;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    batch.swap(tasks_);  // run outside the lock so tasks may post more tasks
  }
  for (Task& t : batch) t();
}

void EventLoop::runTimers() {
  std::vector<Task> due;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = Clock::now();
    while (!timers_.empty() && timers_.begin()->first <= now) {
      due.push_back(std::move(timers_.begin()->second));
      timers_.erase(timers_.begin());
    }
  }
  for (Task& t : due) t();
}

void EventLoop::run() {
  loopThread_ = std::this_thread::get_id();
  while (!quit_) {
    epoll_event events[16];
    int n = ::epoll_wait(epoll_.get(), events, 16, nextTimeoutMs());
    if (n < 0 && errno != EINTR) break;

    for (int i = 0; i < n; ++i) {
      int fd = events[i].data.fd;
      if (fd == wake_.get()) {
        uint64_t counter;
        ssize_t r = ::read(wake_.get(), &counter, sizeof(counter));  // reset the eventfd
        (void)r;
        continue;
      }
      // Copy the callback out under the lock, call it outside: the callback may
      // itself add/remove fds, and removeFd() must not free it while it runs.
      std::shared_ptr<FdCallback> cb;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = fds_.find(fd);
        if (it != fds_.end()) cb = it->second;
      }
      if (cb) (*cb)(events[i].events);  // else: removed after epoll_wait returned; ignore
    }
    runTasks();
    runTimers();
  }
  loopThread_ = std::thread::id();
}

}  // namespace mini

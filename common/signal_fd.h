// Turns signals into a readable file descriptor so they can be handled inside the
// EventLoop like any other event, instead of in an async signal handler.
#pragma once

#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <initializer_list>
#include <optional>

#include "common/event_loop.h"
#include "common/unique_fd.h"

namespace mini {

// Blocks the given signals for the calling thread (and threads it creates later),
// so create it at the top of main(), before starting any thread.
class SignalFd {
 public:
  explicit SignalFd(std::initializer_list<int> signals) {
    sigset_t mask;
    sigemptyset(&mask);
    for (int s : signals) sigaddset(&mask, s);
    pthread_sigmask(SIG_BLOCK, &mask, &oldMask_);
    fd_.reset(::signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK));
  }
  ~SignalFd() { pthread_sigmask(SIG_SETMASK, &oldMask_, nullptr); }
  SignalFd(const SignalFd&) = delete;
  SignalFd& operator=(const SignalFd&) = delete;

  int fd() const { return fd_.get(); }

  // The next pending signal number, or nullopt if none.
  std::optional<int> read() {
    signalfd_siginfo info;
    if (::read(fd_.get(), &info, sizeof(info)) != static_cast<ssize_t>(sizeof(info))) {
      return std::nullopt;
    }
    return static_cast<int>(info.ssi_signo);
  }

  // Block until a signal arrives (for programs without an EventLoop).
  int wait() {
    for (;;) {
      pollfd p{fd_.get(), POLLIN, 0};
      ::poll(&p, 1, -1);
      if (std::optional<int> s = read()) return *s;
    }
  }

 private:
  UniqueFd fd_;
  sigset_t oldMask_;
};

// The usual server shutdown: Ctrl-C or SIGTERM makes the loop return.
inline void quitOnSignal(EventLoop& loop, SignalFd& sig) {
  loop.addFd(sig.fd(), EPOLLIN, [&loop, &sig](uint32_t) {
    sig.read();
    loop.quit();
  });
}

}  // namespace mini

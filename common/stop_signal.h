// Ctrl-C / SIGTERM handling for the single-threaded stage-2 servers: a signal sets
// a flag and interrupts poll(), and the main loop notices.
// TODO(stage 3): replaced by a signalfd inside the EventLoop.
#pragma once

#include <csignal>

namespace mini {

inline volatile std::sig_atomic_t g_stopRequested = 0;

inline bool stopRequested() { return g_stopRequested != 0; }

inline void installStopHandler() {
  struct sigaction sa = {};
  sa.sa_handler = [](int) { g_stopRequested = 1; };
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;  // no SA_RESTART: we want poll() to return EINTR
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
}

}  // namespace mini

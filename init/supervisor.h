// Supervisor: starts services, notices when they exit, restarts crashed ones.
// This is the core of what init does. Real AOSP: init's Service class and the
// `restart`/`critical`/`oneshot` service options.
#pragma once

#include <sys/types.h>

#include <chrono>
#include <deque>
#include <string>
#include <vector>

#include "common/event_loop.h"
#include "init/config.h"
#include "init/property_store.h"

namespace mini {

// Publishes each service's state as properties, like Android:
//   init.svc.<name>            running | restarting | stopping | stopped | failed
//   init.svc_debug_pid.<name>  pid of the most recent instance
//
// Single-threaded: every method runs on the loop thread.
class Supervisor {
 public:
  Supervisor(EventLoop& loop, PropertyStore& props) : loop_(loop), props_(props) {}

  void addService(ServiceDef def);
  void startAll();

  // Call when SIGCHLD arrives: reap exited children and decide what happens next.
  void onChildExit();

  // Begin an orderly shutdown: SIGTERM everything (SIGKILL after a grace period),
  // then quit the loop once all children are gone.
  void stopAll();

 private:
  enum class State { Stopped, Running, Restarting, Stopping, Failed };

  struct Service {
    ServiceDef def;
    State state = State::Stopped;
    pid_t pid = -1;
    std::deque<std::chrono::steady_clock::time_point> recentStarts;  // for crash-loop detection
  };

  Service* find(const std::string& name);
  Service* findByPid(pid_t pid);
  bool anyRunning() const;
  void start(Service& s);
  void scheduleRestart(Service& s);
  void setState(Service& s, State state);

  EventLoop& loop_;
  PropertyStore& props_;
  std::vector<Service> services_;
  bool shuttingDown_ = false;
};

}  // namespace mini

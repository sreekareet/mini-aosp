#include "init/supervisor.h"

#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>

#include <cstring>

#include "common/log.h"
#include "common/paths.h"

extern char** environ;

namespace mini {

namespace {
constexpr const char* kTag = "init";

// Crash-loop protection: a service that is started this many times within the window
// is given up on ("failed") instead of being restarted forever. (Android instead
// reboots into recovery for `critical` services.)
constexpr size_t kMaxStartsInWindow = 5;
constexpr auto kCrashWindow = std::chrono::seconds(10);
constexpr auto kStopGrace = std::chrono::seconds(3);

// A path without '/' means "next to the init binary".
std::string resolvePath(const std::string& path) {
  return path.find('/') == std::string::npos ? exeDir() + "/" + path : path;
}
}  // namespace

void Supervisor::addService(ServiceDef def) {
  Service s;
  s.def = std::move(def);
  services_.push_back(std::move(s));
}

Supervisor::Service* Supervisor::find(const std::string& name) {
  for (Service& s : services_) {
    if (s.def.name == name) return &s;
  }
  return nullptr;
}

Supervisor::Service* Supervisor::findByPid(pid_t pid) {
  for (Service& s : services_) {
    if (s.pid == pid) return &s;
  }
  return nullptr;
}

bool Supervisor::anyRunning() const {
  for (const Service& s : services_) {
    if (s.pid > 0) return true;
  }
  return false;
}

void Supervisor::setState(Service& s, State state) {
  s.state = state;
  const char* name = "stopped";
  switch (state) {
    case State::Running: name = "running"; break;
    case State::Restarting: name = "restarting"; break;
    case State::Stopping: name = "stopping"; break;
    case State::Stopped: name = "stopped"; break;
    case State::Failed: name = "failed"; break;
  }
  props_.set("init.svc." + s.def.name, name);
}

void Supervisor::startAll() {
  for (Service& s : services_) start(s);
}

void Supervisor::start(Service& s) {
  auto now = std::chrono::steady_clock::now();
  while (!s.recentStarts.empty() && now - s.recentStarts.front() > kCrashWindow) {
    s.recentStarts.pop_front();
  }
  if (s.recentStarts.size() >= kMaxStartsInWindow) {
    logError(kTag, "service '%s' keeps crashing; giving up", s.def.name.c_str());
    setState(s, State::Failed);
    return;
  }
  s.recentStarts.push_back(now);

  std::string path = resolvePath(s.def.path);
  std::vector<char*> argv{const_cast<char*>(path.c_str())};
  for (std::string& a : s.def.args) argv.push_back(a.data());
  argv.push_back(nullptr);

  // init blocks SIGCHLD/SIGTERM/SIGINT (it reads them from a signalfd), and a child
  // inherits the blocked mask. Reset it, or the child could never be terminated.
  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  sigset_t none;
  sigemptyset(&none);
  posix_spawnattr_setsigmask(&attr, &none);
  posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK);

  pid_t pid = -1;
  int err = ::posix_spawn(&pid, path.c_str(), nullptr, &attr, argv.data(), environ);
  posix_spawnattr_destroy(&attr);

  if (err != 0) {
    logError(kTag, "cannot start '%s' (%s): %s", s.def.name.c_str(), path.c_str(), std::strerror(err));
    if (s.def.oneshot) setState(s, State::Failed);
    else scheduleRestart(s);  // counts toward the crash-loop limit, so it ends in "failed"
    return;
  }
  s.pid = pid;
  setState(s, State::Running);
  props_.set("init.svc_debug_pid." + s.def.name, std::to_string(pid));
  logInfo(kTag, "started '%s' (pid %d)", s.def.name.c_str(), pid);
}

void Supervisor::scheduleRestart(Service& s) {
  setState(s, State::Restarting);
  loop_.postDelayed(s.def.restartDelay, [this, name = s.def.name] {
    Service* svc = find(name);
    // Re-check: shutdown may have begun while we waited.
    if (svc != nullptr && svc->state == State::Restarting && !shuttingDown_) start(*svc);
  });
}

void Supervisor::onChildExit() {
  for (;;) {
    int status = 0;
    pid_t pid = ::waitpid(-1, &status, WNOHANG);  // reap every child that has exited
    if (pid <= 0) break;
    Service* s = findByPid(pid);
    if (s == nullptr) continue;
    s->pid = -1;

    if (WIFEXITED(status)) {
      logInfo(kTag, "'%s' exited with status %d", s->def.name.c_str(), WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
      logWarn(kTag, "'%s' killed by signal %d", s->def.name.c_str(), WTERMSIG(status));
    }

    if (shuttingDown_ || s->state == State::Stopping || s->def.oneshot) {
      setState(*s, State::Stopped);
    } else {
      scheduleRestart(*s);
    }
  }
  if (shuttingDown_ && !anyRunning()) loop_.quit();
}

void Supervisor::stopAll() {
  if (shuttingDown_) return;
  shuttingDown_ = true;
  logInfo(kTag, "shutting down");

  for (auto it = services_.rbegin(); it != services_.rend(); ++it) {  // reverse start order
    if (it->pid > 0) {
      setState(*it, State::Stopping);
      ::kill(it->pid, SIGTERM);
    } else if (it->state == State::Restarting) {
      setState(*it, State::Stopped);
    }
  }
  if (!anyRunning()) {
    loop_.quit();
    return;
  }
  loop_.postDelayed(std::chrono::duration_cast<std::chrono::milliseconds>(kStopGrace), [this] {
    for (Service& s : services_) {
      if (s.pid > 0) {
        logWarn(kTag, "'%s' ignored SIGTERM; killing it", s.def.name.c_str());
        ::kill(s.pid, SIGKILL);
      }
    }
  });
}

}  // namespace mini

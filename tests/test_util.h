// Helpers shared by the tests.
#pragma once

#include <spawn.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "ipc/stub.h"
#include "ipc/remote.h"
#include "ipc/wire.h"

extern char** environ;

namespace mini::test {

// An isolated runtime directory for sockets, exported via MINI_AOSP_RUN_DIR so
// child processes started by the test use it too.
class TempRunDir {
 public:
  TempRunDir() {
    char tmpl[] = "/tmp/mini-aosp-test-XXXXXX";
    path_ = ::mkdtemp(tmpl);
    ::setenv("MINI_AOSP_RUN_DIR", path_.c_str(), 1);
  }
  ~TempRunDir() {
    ::unsetenv("MINI_AOSP_RUN_DIR");
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

// Runs a program; kills and reaps it on destruction (RAII, so a failing test can't leak processes).
class ChildProcess {
 public:
  ChildProcess() = default;
  ChildProcess(const std::string& path, std::vector<std::string> args = {}) {
    std::vector<char*> argv{const_cast<char*>(path.c_str())};
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    if (::posix_spawn(&pid_, path.c_str(), nullptr, nullptr, argv.data(), environ) != 0) pid_ = -1;
  }
  ChildProcess(ChildProcess&& o) noexcept : pid_(std::exchange(o.pid_, -1)) {}
  ChildProcess& operator=(ChildProcess&& o) noexcept {
    if (this != &o) {
      stop(SIGKILL);
      pid_ = std::exchange(o.pid_, -1);
    }
    return *this;
  }
  ~ChildProcess() { stop(SIGKILL); }

  pid_t pid() const { return pid_; }
  bool started() const { return pid_ > 0; }

  // Send a signal and wait for exit.
  void stop(int sig) {
    if (pid_ <= 0) return;
    ::kill(pid_, sig);
    ::waitpid(pid_, nullptr, 0);
    pid_ = -1;
  }

 private:
  pid_t pid_ = -1;
};

inline bool waitFor(const std::function<bool()>& cond, int timeoutMs = 5000) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (!cond()) {
    if (std::chrono::steady_clock::now() > deadline) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return true;
}

inline bool fileExists(const std::string& p) { return ::access(p.c_str(), F_OK) == 0; }

// An IRemote that calls a stub in the same process. Every call still goes through the
// real wire encoding, so proxy + stub marshalling is tested without any sockets.
// Events pushed by the stub are delivered synchronously to the event handler.
class LoopbackRemote final : public IRemote {
 public:
  explicit LoopbackRemote(IStub& stub) : stub_(stub) {}

  Result<Parcel> transact(uint32_t code, const Parcel& data) override {
    Parcel call = makeCall(code, data);
    Result<uint32_t> c = parseCall(call);
    if (!c) return c.status();

    CallerInfo caller;
    caller.connection = 1;
    caller.sendEvent = [this](uint32_t evCode, const Parcel& args) {
      Parcel frame = makeEvent(evCode, args);
      Result<uint32_t> ev = parseEvent(frame);
      if (!ev || !handler_) return Status::NotFound;
      handler_(ev.value(), frame);
      return Status::Ok;
    };
    Parcel reply;
    Status s = stub_.onTransact(c.value(), call, reply, caller);
    return parseReply(makeReply(s, reply));
  }

  Status setEventHandler(EventHandler handler) override {
    handler_ = std::move(handler);
    return Status::Ok;
  }

  void disconnect() { stub_.onClientGone(1); }  // simulate the client dying

 private:
  IStub& stub_;
  EventHandler handler_;
};

}  // namespace mini::test

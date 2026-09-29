// Error handling for the whole project: error codes, not exceptions.
//
// Why: AOSP native code (Binder, HALs) is built without exceptions and returns
// status_t. We do the same, but with a typed enum and a small Result<T> so a
// forgotten error check is harder to write than with a raw int.
#pragma once

#include <cassert>
#include <cstdint>
#include <optional>
#include <utility>

namespace mini {

enum class Status : int32_t {
  Ok = 0,
  NotFound,
  InvalidArgument,
  ReadOnly,
  NotSupported,
  IoError,
  DeadObject,          // the peer process is gone
  BadParcel,           // malformed or truncated message
  UnknownTransaction,  // the service does not know this method code
  WouldBlock,          // non-blocking I/O: nothing to do right now
  Timeout,             // a call got no reply in time
  IllegalState,        // the call is not allowed right now (e.g. would deadlock)
};

inline const char* toString(Status s) {
  switch (s) {
    case Status::Ok: return "Ok";
    case Status::NotFound: return "NotFound";
    case Status::InvalidArgument: return "InvalidArgument";
    case Status::ReadOnly: return "ReadOnly";
    case Status::NotSupported: return "NotSupported";
    case Status::IoError: return "IoError";
    case Status::DeadObject: return "DeadObject";
    case Status::BadParcel: return "BadParcel";
    case Status::UnknownTransaction: return "UnknownTransaction";
    case Status::WouldBlock: return "WouldBlock";
    case Status::Timeout: return "Timeout";
    case Status::IllegalState: return "IllegalState";
  }
  return "Unknown";
}

// Either a value or an error Status (never Status::Ok without a value).
// A tiny std::expected stand-in (std::expected is C++23).
template <typename T>
class Result {
 public:
  Result(T value) : value_(std::move(value)), status_(Status::Ok) {}  // NOLINT: implicit on purpose
  Result(Status error) : status_(error) { assert(error != Status::Ok); }  // NOLINT

  bool ok() const { return status_ == Status::Ok; }
  explicit operator bool() const { return ok(); }
  Status status() const { return status_; }

  T& value() {
    assert(ok());
    return *value_;
  }
  const T& value() const {
    assert(ok());
    return *value_;
  }
  T take() {  // move the value out (for move-only types)
    assert(ok());
    return std::move(*value_);
  }
  T valueOr(T fallback) const { return ok() ? *value_ : std::move(fallback); }

 private:
  std::optional<T> value_;
  Status status_;
};

}  // namespace mini

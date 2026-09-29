// Minimal logging to stderr, standing in for Android's logcat/ALOGx.
// printf-style so it needs no dependencies; the format attribute lets the
// compiler check the arguments.
#pragma once

#include <cstdarg>
#include <cstdio>

namespace mini {

namespace detail {
inline void vlog(const char* level, const char* tag, const char* fmt, va_list ap) {
  char msg[512];
  std::vsnprintf(msg, sizeof(msg), fmt, ap);
  std::fprintf(stderr, "%s/%s: %s\n", level, tag, msg);  // one call => lines don't interleave
}
}  // namespace detail

__attribute__((format(printf, 2, 3))) inline void logInfo(const char* tag, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  detail::vlog("I", tag, fmt, ap);
  va_end(ap);
}

__attribute__((format(printf, 2, 3))) inline void logWarn(const char* tag, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  detail::vlog("W", tag, fmt, ap);
  va_end(ap);
}

__attribute__((format(printf, 2, 3))) inline void logError(const char* tag, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  detail::vlog("E", tag, fmt, ap);
  va_end(ap);
}

}  // namespace mini

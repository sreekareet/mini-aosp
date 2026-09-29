# Design decisions

One entry per stage: the decision, alternatives, and the trade-off. Assumptions I
made where the brief was ambiguous are marked **Assumption**.

## Cross-cutting

- **Error handling: error codes, not exceptions, in every layer.** `Status` enum plus
  `Result<T>` (`common/result.h`). Matches Android native code (`status_t`), keeps
  control flow explicit, and works across the `.so`/socket boundaries.
  Alternative: exceptions (less code, but invisible failure paths, and unsafe across
  a plugin ABI). Trade-off: more `if (!r)` checks.
- **Assumption: a small `common/` directory.** The brief's layout has no place for
  shared utilities (Result, logging, paths, later the event loop and RAII fd).
  Added `common/`, the analogue of AOSP's `libutils`/`libbase`. Header-only where possible.
- **Assumption: includes are rooted at the repo top** (`#include "hal/vehicle_hal.h"`),
  so a file's layer is visible at every include.
- **Assumption: C++17 throughout.** No stage clearly needed C++20.
- **Build layout:** executables in `build/bin`, shared libraries in `build/lib`;
  programs locate siblings relative to `/proc/self/exe`. `-Werror` is on by default
  (`-DMINI_AOSP_WERROR=OFF` to relax); GoogleTest is fetched at tag v1.17.0
  (`-DMINI_AOSP_SYSTEM_GTEST=ON` uses `libgtest-dev` instead).

## Stage 1: HAL

- **Passthrough HAL: C++ virtual interface in a `.so`, loaded with `dlopen`.**
  Alternatives: C function-table ABI, PIMPL + C API, out-of-process HAL over IPC.
  Chose the virtual interface because it is the most readable. Trade-off: plugin and
  loader must share compiler and stdlib. Stage 2 shows the out-of-process answer
  for the layers above.
- **Plugin ABI = three C symbols** (version, create, destroy); the plugin allocates
  and frees its own object.
- **`LoadedHal` couples library and object** so the library provably outlives the
  object; move-assignment is deleted because the defaulted one would violate that.
- **Property values are `std::variant<bool, int32_t, float>`** rather than a
  string or a union: type-safe and small. Trade-off: no arrays/strings/vectors
  (real VHAL has them).
- **Assumption: four properties** (speed, fuel level, gear, HVAC temperature);
  speed and fuel are read-only from the client's view.

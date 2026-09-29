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

## Stage 2: IPC

- **Unix stream sockets with length-prefixed frames** instead of emulating the Binder
  driver. Alternatives: `SOCK_SEQPACKET` (keeps message boundaries for free, but less
  familiar and awkward for large messages), shared memory. Trade-off: more copies, no
  kernel-verified identity (stage 5 uses `SO_PEERCRED`).
- **ServiceManager returns a socket path, not a proxied connection.** The address plays
  the role of a Binder handle. Re-registering a name replaces it, so a restarted service takes over.
- **Error model in the IPC layer: same `Status`/`Result<T>`.** The callee's `Status` is carried in the Reply frame and returned to the caller unchanged.
- **Sticky-error `Parcel`** (like `std::istream`): read everything, check `ok()` once.
  Alternative: `Result<T>` per read (explicit, but drowns the marshalling code).
- **Proxy/stub written by hand** around one shared `IVehicleService` interface; no code generator (brief forbids it).
- **Assumption: `IStub::onTransact` takes `Parcel&` (non-const)** because reading advances the cursor.
- **Assumption: sockets live in `/tmp/mini-aosp-<uid>/`** (`MINI_AOSP_RUN_DIR` overrides; tests use a temp dir). Sockets are mode 0666 by design (see study notes).
- **Assumption: a service registers *after* it starts listening**, and retries for up to 10 s if the ServiceManager is not up yet (real AOSP: `waitForService`).
- **Single-threaded `poll()` server** for stage 2, as the brief requires; replaced by the epoll event loop in stage 3.
- **Accepted limitation:** the ServiceManager never learns that a service died, so a stale
  entry stays until the name is re-registered. Clients see `NotFound`/`DeadObject` when connecting. Stage 5 handles reconnect.

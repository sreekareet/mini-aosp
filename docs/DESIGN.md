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

## Stage 3: Framework, event loop, subscriptions

- **`EventLoop` lives in `common/`** (not `framework/` as the brief's layout suggests):
  `ipc` depends on it and must not depend on the layer above. AOSP's `Looper` is in `libutils` for the same reason.
- **epoll + eventfd + a timer map** (no `timerfd`): the timeout for `epoll_wait` is computed from
  the earliest timer. Fewer fds and less code than one timerfd per timer.
- **One loop thread, callbacks never overlap** (Looper model). Servers are lock-free because
  all their state is confined to that thread. Alternative: a thread pool (stage 6 exercises).
- **Events share the client connection; `RpcClient` demultiplexes** replies from events with a reader on the loop.
  Alternative: dedicated event socket. One call in flight per client (no call ids).
- **`RpcClient::transact` returns `IllegalState` on the loop thread** rather than deadlocking; **timeouts close the connection** (5 s).
- **`weak_ptr` capture in the loop callback** so a callback racing with client destruction is harmless.
- **Callback interface `IPropertyListener` with `shared_ptr`** across the service boundary: the caller
  and the service both keep it alive (the `sp<>` role). App-level API uses `std::function` instead.
- **Subscription semantics:** events fire only when the value *changes*; the server keeps one
  subscription per (connection, property) and the proxy fans out to any number of local listeners.
- **`IStub::onClientGone`** removes a dead client's subscriptions (stand-in for `linkToDeath`).
- **HAL contract change (ABI 2):** `setChangeCallback` + `poll()`. `poll()` exists so the fake HAL
  can simulate driving without owning a thread; a real HAL would call the callback from its own thread.
- **Typed `PropertyKey<T>`** with an untyped fallback API for generic tools.
- **Assumption:** the fake HAL simulates the car: gear D (3) accelerates 5 km/h per 200 ms tick to 60, other gears
  decelerate to 0, fuel drains 0.05% per tick while moving.
- **Sanitizers:** `MINI_AOSP_SANITIZER=address|thread`. Suites pass under both (locking design is in `stage-3.md`).
- **Accepted limitation:** a client that stops reading its socket can stall the service for up to 1 s per
  event (`sendFrame` waits, then drops the connection). A real system would queue per client.

## Stage 4: Init and properties

- **`init` is an ordinary process, not PID 1** (assumption): shutdown is SIGTERM; children are stopped
  in reverse start order and force-killed after 3 s. If init is SIGKILLed its children are orphaned (accepted).
- **Config language: three directives plus two service options** (`setprop`, `service`, `oneshot`,
  `restart_delay`). Parse errors carry line numbers. Alternative: reuse a real format (JSON/INI); a
  line-oriented file resembles `init.rc` and needs no dependency. Not implemented: triggers, classes (exercise).
- **Property service hosted in init**, reusing `RpcServer`/`EventLoop`; **reads also go through IPC** (real AOSP maps shared memory
  for reads). Simpler; documented as a difference.
- **Property rules:** keys `[A-Za-z0-9._-]` up to 64 chars, values up to 92, `ro.*` write-once. Plain map, no locking (single-threaded owner).
- **Crash-loop protection:** 5 starts within 10 s -> `failed` permanently. Alternative: exponential backoff.
- **`posix_spawn` with a reset signal mask** instead of fork/exec; **`signalfd`** for SIGCHLD/SIGINT/SIGTERM.
- **Service state is published as `init.svc.<name>`/`init.svc_debug_pid.<name>`** (Android's names).
- **Assumption:** a path without `/` in `init.conf` is relative to the init executable; the config is copied to `build/etc/init.conf`.
- **Assumption:** `ChildProcess` in tests sends SIGTERM first, so test failures do not leave orphaned services.
- **Accepted limitation:** services register with the ServiceManager once at startup, so a ServiceManager crash
  is not recovered (init restarts it, but the registry is empty until services are restarted).

## Stage 5: Robustness

- **Death is detected by the connection closing** (EOF/HUP on the loop), surfaced as `IRemote::linkToDeath`. No heartbeat protocol; a
  hung-but-alive service is not detected (calls just time out after 5 s and close the connection).
- **Reconnect lives in `PropertyManager`**, on a dedicated worker thread, with 100 ms -> 2 s exponential backoff. The death callback
  (loop thread) only records the event, because reconnecting needs synchronous calls, which are forbidden on the loop thread.
- **The manager owns subscriptions, not the connection**, and replays them on every new connection. Calls fail fast with `DeadObject` while
  disconnected. State inside the restarted service is *not* restored (documented; apps must tolerate resets).
- **Concurrency design for reconnect:** `subscriptionMutex_` (subscribe/unsubscribe/reconnect) before `mutex_` (connection state);
  per-connection `Attempt` tokens so a stale death notice cannot tear down a healthy connection; death recipient registered *before* the
  connection is used, and re-checked at install time. Stress-tested under TSan (25 repeats).
- **API ordering guarantee is deliberately weak:** `isConnected()`/calls may succeed slightly before the `connected=true` callback is delivered (documented on the header).
- **Identity: `SO_PEERCRED` read once at accept.** Alternative: per-message identity (Binder). Pitfalls (fd passing, pid reuse) are in the study notes.
- **Permissions: uid allow-lists, fail closed, checked before any work.** `UidAllowList`: root always allowed, empty list = root only.
  Alternative: named permissions plus a policy service (closer to Android, much larger).
- **Policies chosen (assumptions):** vehicle *writes* need an allowed uid (default: the user running the service; `--allow-writer` overrides), reads and
  subscriptions are open; ServiceManager `addService` only for its owner/root; properties: `ro.*`/`init.svc.*` never via IPC, `sys.*`/`debug.*` anyone, else owner/root.
- **Sockets stay mode 0666** so other uids can connect and be *refused by the service* (the Binder model).
- **Accepted limitations:** ServiceManager restart empties the registry and services do not re-register; a HAL crash kills the service (passthrough
  HAL); no `clearCallingIdentity` equivalent because services never call onward on a client's behalf; the cross-uid path could not be tested with a real second user in the
  development environment (the same code path is covered by an end-to-end test using an allow-list that excludes the test's own uid).

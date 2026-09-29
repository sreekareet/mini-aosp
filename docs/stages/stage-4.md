# Stage 4: Mini init and a property store

## 1. What was built and why

- `init` (`init/init_main.cpp`): reads `init.conf`, sets boot properties, runs the property
  service, starts the services in order, restarts crashed ones, and shuts them down on SIGTERM.
- `parseInitConfig` (`init/config.*`): the config format (`setprop`, `service`, `oneshot`, `restart_delay`).
- `Supervisor` (`init/supervisor.*`): spawn, reap (SIGCHLD), restart with a delay, crash-loop
  protection, orderly stop. Publishes `init.svc.<name>` and `init.svc_debug_pid.<name>`.
- `PropertyStore` (`init/property_store.*`): string key/value map with key validation and
  write-once `ro.*` properties.
- Property service (`init/property_service.*`): the store exposed over IPC (stub + proxy), hosted
  inside init on the same `EventLoop` and `RpcServer` as everything else.
- `framework/SystemProperties` plus `getprop` / `setprop` apps.

Why: init is what turns a set of binaries into a system: it starts things in order, owns
their lifecycle, and publishes state through properties. Properties are Android's
lightweight system-wide configuration and status channel.

## 2. Reading order

1. `init/init.conf`, then `init/config.h` / `.cpp` - the language init understands.
2. `init/property_store.*` - the data structure and its rules.
3. `init/property_service.*` - the same proxy/stub pattern as stage 2, now for properties.
4. `init/supervisor.*` - the heart: `start`, `onChildExit`, `scheduleRestart`, `stopAll`.
5. `init/init_main.cpp` - how the pieces are wired onto one `EventLoop` + `SignalFd`.
6. `framework/system_properties.*`, `apps/getprop.cpp`, `apps/setprop.cpp`.
7. `tests/init_test.cpp` (bottom half runs the real `init`).

## 3. C++ concepts used

| Concept | Where |
|---|---|
| Reusing the event loop for a new job (signals, timers, IPC on one thread) | `init_main.cpp`, `supervisor.cpp` |
| RAII around C APIs: `SignalFd`; `posix_spawnattr_t` init/destroy pair | `common/signal_fd.h`, `Supervisor::start` |
| `enum class` for state, mapped to strings in one place | `Supervisor::State`, `setState` |
| Structured bindings over `(key, value)` pairs | `getprop.cpp`, `PropertyServiceStub` |
| `std::from_chars`, `std::string_view` parsing with explicit error messages | `config.cpp` |
| Lambdas capturing `this` + a *name* (not a pointer) so vector reallocation cannot dangle | `scheduleRestart` |
| Lambda-returning-status pattern for parse errors | `parseInitConfig` (`fail` lambda) |
| `Result<T>` with a by-value proxy | `Result<PropertyServiceProxy>` in `system_properties.cpp` |
| `std::deque` as a sliding time window | `Service::recentStarts` |
| Test helper RAII (`ChildProcess`, `TempRunDir`) | `tests/test_util.h` |

## 4. Mapping to real AOSP

| Here | AOSP |
|---|---|
| `init.conf` | `init.rc` (actions, triggers, `on boot`, `service ... class ...`) |
| `service` / `oneshot` / `restart_delay` | `service` options; `oneshot`; default restart-on-exit; `critical` (reboot into recovery) |
| Crash loop -> `failed` | `critical` services that crash 4 times in 4 minutes reboot to recovery; others just restart |
| `init.svc.<name>` and `init.svc_debug_pid.<name>` | exactly these property names |
| `ro.*` write-once | same rule (enforced by the property area's semantics) |
| Property service over a socket in init | `property_service` socket in init; writers go through it |
| `SystemProperties::get` connects per call | Reads use a shared-memory property area mapped into every process - no IPC. Only writes go to init |
| `getprop` / `setprop` output format | same `[key]: [value]` |
| `SignalFd` for SIGCHLD | init handles SIGCHLD in its epoll loop the same way |

Where AOSP is more complex: `on property:x=y` triggers (init reacts to properties), `class` groups
(`class_start main`), user/group/capabilities/SELinux contexts per service, namespaces and cgroups, file
creation for sockets (`socket` option), a multi-stage boot (first-stage init, selinux setup, second-stage),
and `ctl.start` / `ctl.stop` properties for controlling services at runtime. Here init runs as an
ordinary process, not PID 1, so there are no orphan children to reap and no mounting.

## 5. Design decisions and trade-offs

- **Property service lives in init**, served with the same `RpcServer`, so properties reuse
  stage-2/3 code. Alternative: shared memory. Simpler is better here.
- **Who may write what** is decided in stage 5 (`canSetProperty`): `ro.*` and `init.svc.*` can then only be written by init itself (from `init.conf`), never over IPC.
- **Plain map, no locking.** The store is touched only on init's loop thread.
- **One `setprop`-style rule set**: `[A-Za-z0-9._-]` keys up to 64 chars, values up to 92, `ro.*` write-once.
- **Crash-loop protection**: 5 starts within 10 s -> `failed`, and stay failed. Alternative:
  exponential backoff (more forgiving); `failed` is easier to see in a demo and to test.
- **Restart via `postDelayed`** on the loop (not `sleep`): init stays responsive to other events while waiting.
- **Signals through `signalfd`** so SIGCHLD is handled in normal code, not a signal handler
  (no async-signal-safety rules). Children get the signal mask reset (`posix_spawn` attr);
  otherwise they would inherit init's blocked SIGTERM and be unkillable.
- **Service state tracked by name lookup, not stored pointers**, since `services_` is a vector.
- **Path without `/` = next to the init binary**, so the same config works from any build directory.
- **`posix_spawn` instead of `fork` + `exec`**: no async-signal-safety pitfalls between fork and exec, and it reports a missing binary immediately.
- **Assumption: init is an ordinary process** (not PID 1); shutdown is SIGTERM to init, which
  stops children in reverse start order and force-kills after 3 s.
- **Assumption: `vehicle_service` retries the ServiceManager** (from stage 2), so start order in `init.conf` is not a race.
- **Accepted limitation:** if init itself is SIGKILLed its children are orphaned. (On real Android init is PID 1; if it dies the kernel panics.)
- **Accepted limitation:** the ServiceManager is not restarted-aware: a restarted service simply re-registers under its name. If the ServiceManager itself crashes, services do not re-register (they only register once at start).

## 6. Interview questions

1. **What does init do on Android?** Mounts filesystems, loads SELinux policy, parses `init.rc` files, starts the property service, and starts/monitors services; restarts them on death and reacts to property changes via triggers.
2. **How does init know a service died?** SIGCHLD; it calls `waitpid(-1, WNOHANG)` in a loop (signals can coalesce, so one signal can mean several dead children) and matches pids to services.
3. **What is a zombie process and how do you avoid it?** A child that exited but has not been `wait`ed for. Always reap (`waitpid`) - init also adopts and reaps orphans when it is PID 1.
4. **Why `signalfd`/self-pipe rather than a signal handler?** Handlers may only call async-signal-safe functions and interrupt arbitrary code. Turning the signal into an fd event lets normal code handle it in the loop.
5. **What are system properties and how do they differ from settings/SharedPreferences?** A global key/value store for low-level system state and configuration (`ro.*` build info, `sys.*`, `persist.*`, `init.svc.*`), readable by everyone (via shared memory) but writable only through init, subject to SELinux. Not per-app, not durable except `persist.*`.
6. **Why are `ro.*` properties write-once?** They describe fixed facts (build fingerprint, hardware) that must not change after boot; the property service refuses a second write.
7. **How do you avoid a crash loop hammering the device?** Delay before restart, cap restarts in a time window, and escalate (Android: reboot to recovery for critical services; here: mark `failed`).
8. **What is `oneshot`?** The service runs once and is not restarted when it exits (e.g. a setup step).
9. **Why does a spawned child need its signal mask reset?** The mask is inherited across `exec`. If init blocked SIGTERM, every child would start with it blocked.

## 7. Exercises

1. **Boot it by hand.** `export MINI_AOSP_RUN_DIR=$(mktemp -d); ./build/bin/init &` then `./build/bin/getprop`.
   Expected: both services `running`, their pids, `sys.boot_completed = 1`, `ro.build.type = learning`.
2. **Kill the service.** `kill -9 $(./build/bin/getprop init.svc_debug_pid.vehicle_service)`, wait a second, run
   `vehicle_cli get gear`. Expected: `init.svc_debug_pid.vehicle_service` changes, the CLI works again, and the
   gear is back to 0 (the service's state died with it - see stage 5 for how clients cope with the outage).
3. **Trigger the crash-loop limit.** Add `service bad /bin/false` (with `restart_delay 100`) to `init.conf`, re-run cmake (it copies
   the file) and boot. Expected: `init.svc.bad` goes `restarting` several times, then `failed`; other services are unaffected.
4. **Add a service.** Write `apps/heartbeat.cpp` that loops calling `setprop sys.heartbeat <n>` every second (use `SystemProperties::set`), add a
   `service heartbeat heartbeat` line, and watch `getprop sys.heartbeat` change.
5. **Implement `on property:` triggers (harder).** Extend `config.h` with `on property:<key>=<value>` followed by
   `start <service>`; make `PropertyStore::set` notify a callback that init connects to the supervisor. Expected:
   `setprop sys.start_heartbeat 1` starts a service that was declared but not auto-started.

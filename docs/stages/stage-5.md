# Stage 5: Robustness - death notification, reconnect, `SO_PEERCRED` permissions

## 1. What was built and why

**Death and recovery**
- `IRemote::linkToDeath(recipient)`, implemented in `RpcClient`: when the loop sees EOF/HUP the
  recipient is called once (on the loop thread). Linking to an already-dead remote returns `DeadObject`.
- `PropertyManager` now survives a service restart: a **reconnect worker thread** re-looks-up the service
  with exponential backoff (100 ms -> 2 s), re-registers every subscription, and reports transitions through
  `setConnectionCallback(bool connected)`. While the service is down calls return `DeadObject`.
- `vehicle_cli watch` prints `[service died]` / `[service back]` and keeps streaming afterwards.

**Identity and permissions**
- `Connection::peerCredentials()` reads `SO_PEERCRED`; `RpcServer` records the peer's uid/pid at accept time
  and passes them to stubs in `CallerInfo`.
- `UidAllowList` (`ipc/access_policy.h`): root always passes; default-constructed allows nobody else.
- Three policies: vehicle **writes** only for allowed uids (`--allow-writer`, default = the user running the
  service); **ServiceManager registration** only for its owner/root; **properties** via `canSetProperty`
  (`ro.*`/`init.svc.*` never through IPC, `sys.*`/`debug.*` anyone, the rest owner/root).
- New status `PermissionDenied`.

Why: a real system spends most of its code on the unhappy paths. Services crash and get restarted by init;
clients must notice and recover. And because every local process can connect to a service, the service must
ask *who* is calling before it acts.

## 2. Reading order

1. `ipc/remote.h` (`linkToDeath`), then `ipc/rpc_client.cpp` (`markDead`, `linkToDeath`).
2. `framework/property_manager.h` (read the threading and lock-order comments), then `.cpp`:
   `tryConnect` -> `onDeath` -> `workerLoop`, then `subscribeRaw`/`unsubscribe`.
3. `ipc/transport.cpp` (`peerCredentials`), `ipc/stub.h` (`CallerInfo`), `ipc/rpc_server.cpp` (where the uid is captured).
4. `ipc/access_policy.h`, then its three users: `services/vehicle/vehicle_service.cpp` (`kSetProperty`),
   `init/property_service.cpp` (`canSetProperty`), `ipc/service_manager.cpp` (`kAddService`).
5. Tests: `tests/framework_test.cpp` (`ReconnectsAfterServiceRestart...`), `tests/ipc_test.cpp` (`RpcClientTest`),
   `tests/service_test.cpp` (`ServiceRefusesWritesFromAnUnlistedUid`), `tests/init_test.cpp` (bottom test).

## 3. C++ concepts used

| Concept | Where |
|---|---|
| Worker thread with a `condition_variable` predicate; interruptible sleep (`wait_for` on the stop flag) | `PropertyManager::workerLoop` |
| Lock ordering rule (`subscriptionMutex_` before `mutex_`) and why | `property_manager.h` |
| Shared ownership for "a call may outlive a reconnect" (`shared_ptr<VehicleServiceProxy>` copied under a lock) | `currentProxy()` |
| Tagging callbacks with the object they belong to (`Attempt`) so a stale notification is ignored | `tryConnect` / `onDeath` |
| Destruction order as a correctness tool: stop worker, stop loop, then destroy members | `~PropertyManager` |
| `std::function` callbacks invoked outside locks | `RpcClient::State::markDead`, `workerLoop` |
| Value type with safe default: fail-closed `UidAllowList`; named constructors (`anyone()`, `only({...})`) | `ipc/access_policy.h` |
| `std::initializer_list` parameter | `UidAllowList::only` |
| Move-only handling of a moved-from `shared_ptr` (`std::move` then rely on it being null) | `workerLoop` |
| `GTEST_SKIP` for environment-dependent tests | `ServiceRefusesWritesFromAnUnlistedUid` |

## 4. Mapping to real AOSP

| Here | AOSP |
|---|---|
| `IRemote::linkToDeath` + recipient | `IBinder::linkToDeath` / `DeathRecipient::binderDied()` (Java: `IBinder.DeathRecipient`) |
| `DeadObject` | `DEAD_OBJECT` / `DeadObjectException` (and `TransactionTooLargeException` etc.) |
| Reconnect worker + resubscribe | Car API: `CarServiceLifecycleListener`, `Car.createCar(..., CAR_WAIT_TIMEOUT...)`; `CarPropertyManager` re-registers listeners after `onServiceDisconnected/Connected`; `waitForService` |
| Restarting the service | init restarts it; `system_server` restarts ⇒ apps are killed and restarted entirely |
| `SO_PEERCRED` uid/pid | Binder driver stamps every transaction with the caller's uid/pid: `Binder.getCallingUid()` / `getCallingPid()` |
| `UidAllowList` | `checkCallingPermission(...)` / `enforceCallingPermission(...)`, package permissions; SELinux on top |
| ServiceManager registration policy | SELinux `service_manager { add }` rules; only certain domains may register a name |
| Property namespaces (`ro.`, `sys.`, ...) | property contexts + SELinux `property_service { set }` per property prefix |

Where AOSP is more complex: the kernel supplies the identity *per transaction* and it can't be spoofed or
inherited by accident (`SO_PEERCRED` gives identity of whoever *connected*; if a socket is passed to
another process the identity is still the original's - Binder avoids this class of bug), permissions are named
strings mapped to apps by the package manager and checked in the framework, `Binder.clearCallingIdentity()`
handles the "confused deputy" problem when a service calls onward, and SELinux enforces policy even against root.
Death notification in Binder is delivered by the kernel for any remote object, including callback objects the
service holds (so services also learn when a *client* dies; here `onClientGone` covers that).

## 5. Design decisions and trade-offs

- **Death is detected by the connection closing.** Zero extra protocol. Trade-off: it only detects *process*
  death (or an explicit close), not a hung-but-alive service (that needs heartbeats/timeouts; our 5 s call timeout only closes the connection).
- **The death recipient runs on the loop thread and only records the fact**; all slow work (lookup, connect,
  resubscribe - which are synchronous calls) happens on a dedicated **worker thread**. That is forced by the
  rule from stage 3: you cannot make a synchronous call from the loop thread.
- **Subscriptions are owned by the manager, not the connection**, so they can be replayed on a new connection.
  `subscribe`/`unsubscribe` are serialised against reconnecting with `subscriptionMutex_` so a subscription
  made *during* a reconnect can never be lost (found by reasoning about the interleavings; see the `Attempt` and lock-order comments).
- **`Attempt` tokens** tie a death notice to the connection that produced it. Otherwise the death of an
  abandoned attempt could make the manager tear down a healthy new connection, or a death that races with
  installing could be missed (`died` is checked when installing).
- **Calls fail fast with `DeadObject` while disconnected** rather than blocking until reconnection. Apps that
  want to wait can use the connection callback. Alternative: block with a timeout (like `waitForService`).
- **State is not restored after restart.** The new process starts from the HAL's defaults; the manager only
  restores *subscriptions*. Applications must treat values as possibly reset (documented on `PropertyManager`).
- **Backoff 100 ms -> 2 s, doubling.** Quick recovery from a fast restart without hammering while init waits.
- **Permission model = uid allow-lists, checked in the stub before doing any work.** Fail closed (empty list = only root).
  Alternative: named permissions + a policy service (much closer to Android, much bigger).
- **Reads and subscriptions are open to every uid; writes are restricted.** Simple, and enough to show the check.
- **Sockets stay world-connectable (0666)** so a different uid *can* connect and be refused by the service,
  the same model as Binder (device node open to everyone, checks in services).
- **`SO_PEERCRED` is read once at accept time** - cheap and race-free for the connection, but see caveats below.
- **Accepted limitations:** (1) if the ServiceManager itself restarts its registry is empty and running
  services do not re-register (real Android treats servicemanager death as a fatal, system-restarting event); the
  vehicle socket path is fixed, so a *restarted vehicle service* is still found by the reconnect (it re-registers
  and re-binds the same path). (2) A HAL crash takes the whole service process with it (passthrough HAL); AOSP's
  binderized HALs isolate that. (3) No `clearCallingIdentity` equivalent: services never call other services on
  behalf of a client here. (4) `SO_PEERCRED` pid can be reused after the peer exits - use uid for decisions, pid for logging.

## 6. Interview questions

1. **How does an Android app learn its service died?** It registers an `IBinder.DeathRecipient` via `linkToDeath`; the Binder driver calls `binderDied()` when the remote process dies. Calls made afterwards throw `DeadObjectException` (`DEAD_OBJECT` in native).
2. **How should a client handle a service restart?** Detect death, drop the stale proxy, re-acquire the service (`waitForService`/reconnect with backoff), re-register callbacks/listeners, and refresh cached state - the new process has none of the old state.
3. **How does a service know who is calling?** `Binder.getCallingUid()`/`getCallingPid()`, stamped by the kernel per transaction, then `enforceCallingPermission` / package permission checks. Here `SO_PEERCRED` plays the kernel's role.
4. **What is the confused-deputy problem and `clearCallingIdentity`?** When service A calls service B on behalf of a client, B sees A's identity, not the client's - or, if A forwards the client's identity carelessly, the client gains A's privileges. `clearCallingIdentity()` explicitly resets to the service's own identity around such calls.
5. **`SO_PEERCRED` vs Binder identity - what are the pitfalls?** Credentials are captured at `connect()` time for the connection, so an fd handed to another process keeps the old identity; pids can be reused; and it is uid-only (no SELinux context - that is `SO_PEERSEC`). Binder identity is per transaction and kernel-enforced.
6. **Why check permissions before parsing/doing the work?** Don't spend resources or leak information (parse errors, timing) for callers who are not allowed; and it keeps the check in one obvious place.
7. **Why can't the death callback just reconnect?** It runs on the loop thread; reconnecting needs synchronous calls, which would deadlock the loop. So it hands off to a worker thread.
8. **What if death races with setting things up?** Register the death recipient *before* using the connection (so a death is never missed), and re-check the flag when installing the new connection (so a death before install is not lost).
9. **What does `init` do when a service keeps crashing, and why?** Rate-limits restarts and gives up or escalates (here: `failed`; Android: reboot to recovery for critical services) to avoid a busy crash loop that drains battery and hides the real bug.
10. **Why are property namespaces (`ro.`, `sys.`, ...) enforced by the service and not by file permissions?** One socket serves everyone, so the policy must depend on who is asking *and* which key they want; that is exactly what property contexts + SELinux do on Android.

## 7. Exercises

1. **Watch a service crash.** Boot with `init`, run `vehicle_cli set gear 3; vehicle_cli watch speed` and, in another terminal,
   `kill -9 $(getprop init.svc_debug_pid.vehicle_service)`. Expected: `[service died]`, a pause of well under a second, `[service back]`,
   then no speed events until you run `vehicle_cli set gear 3` again (the restarted HAL is in Park: state was lost).
2. **Cross-user permission check** (needs `sudo`; not run by the author - the automated equivalent is `EndToEndTest.ServiceRefusesWritesFromAnUnlistedUid`,
   which uses `--allow-writer` with an uid nobody has). Use the default socket directory, which is world-readable, and copy the client somewhere `nobody` can execute
   (your home directory is usually private):
   `cp build/bin/vehicle_cli /tmp/`, start `init` without `MINI_AOSP_RUN_DIR` set, then
   `sudo -u nobody env MINI_AOSP_RUN_DIR=/tmp/mini-aosp-$(id -u) /tmp/vehicle_cli get gear` (works: reads are open) and
   `... /tmp/vehicle_cli set gear 3`. Expected: `PermissionDenied`, and a service log line `uid <nobody's uid> (pid ...) denied`.
   Then restart `vehicle_service --allow-writer <nobody's uid>` and retry: `Ok`.
3. **Hijack attempt** (same setup as above). As `nobody`, write a 10-line client that calls `registerService("vehicle", "/tmp/evil.sock")`. Expected:
   `PermissionDenied`, a `may not register services` line in the ServiceManager log, and `vehicle_cli` still reaches the real service. (Starting a second
   `vehicle_service` as `nobody` would fail even earlier: it cannot replace the socket file in a directory it does not own - a second, filesystem-level defence.)
4. **Crash the ServiceManager (limitation).** Kill `servicemanager` under `init`. Expected: init restarts it, but `vehicle_cli` now fails with
   `NotFound` until `vehicle_service` is restarted, because services only register once. Fix it: make `vehicle_service` hold an `RpcClient` to the
   ServiceManager with `linkToDeath` and re-register on death.
5. **Break the lock order on purpose.** In `PropertyManager::subscribeRaw`, take `mutex_` (via `currentProxy()`) *before* `subscriptionMutex_`. Run
   `build-tsan` `framework_tests`. Expected: TSan reports a lock-order inversion, or the reconnect test occasionally hangs.
6. **Make `PropertyManager` wait for the service.** Add `template <class T> Result<T> getWait(key, timeout)` that blocks on the connection
   state (use `cv_`) instead of failing with `DeadObject`. Expected: a call during an outage returns the value shortly after `[service back]`.

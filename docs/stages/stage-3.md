# Stage 3: Framework manager, `subscribe()` callbacks, epoll event loop (first threads)

## 1. What was built and why

- `EventLoop` (`common/event_loop.*`): epoll + eventfd wake-up + posted tasks + timers.
  `LoopThread` runs one on its own thread. `SignalFd` turns signals into loop events.
- `RpcServer` moved onto the loop (the stage-2 `poll()` loop and signal flag are gone).
- Server-to-client **events** (a third frame type) and `IStub::onClientGone`.
- `RpcClient`: a connection with a background reader that separates *replies* (wake the
  waiting caller) from *events* (call a handler).
- HAL change callbacks + `poll()` (HAL **ABI version 2**); the fake HAL now simulates
  driving (Drive gear -> accelerates to 60 km/h, burns fuel).
- `IVehicleService::subscribe/unsubscribe`, with `IPropertyListener` as the callback
  interface; proxy-side listener table, stub-side per-connection cleanup.
- `framework/PropertyManager`: the app-facing API with type-safe `PropertyKey<T>`,
  `get/set/subscribe`. `vehicle_cli` now uses it and gained `watch`.
- `-DMINI_AOSP_SANITIZER=thread` (TSan); all tests are clean under ASan/UBSan and TSan.

Why: real Android apps don't poll; they register a listener and the framework calls
it back when the HAL reports a change. That needs a looper thread (client side) and
event push (service side), which is where concurrency enters the project.

## 2. Reading order

1. `common/event_loop.h` - the threading rules are written at the top; `.cpp` after.
2. `common/signal_fd.h` - small, then see its use in `ipc/servicemanager_main.cpp`.
3. `ipc/wire.*` (Event frame), `ipc/stub.h` (`sendEvent`, `onClientGone`).
4. `ipc/rpc_server.*` - now loop-driven.
5. `ipc/rpc_client.*` - the most delicate file: reply/event demultiplexing and locking.
6. `hal/vehicle_hal.h` (callback, `poll`), `hal/fake_vehicle_hal.cpp` (simulation).
7. `services/vehicle/vehicle_iface.*` (proxy listener table), `vehicle_service.*` (fan-out, `RemoteListener`, `onClientGone`), `vehicle_service_main.cpp` (the timer tick).
8. `framework/property_manager.*`, `apps/vehicle_cli.cpp`.
9. Tests: `tests/common_test.cpp`, then `framework_test.cpp`.

## 3. C++ concepts used

| Concept | Where |
|---|---|
| `std::thread`, `std::mutex`, `std::condition_variable`, `lock_guard`/`unique_lock` | `RpcClient::State`, `EventLoop`, `ListenerTable` |
| `std::atomic` (flags and `thread::id`) | `EventLoop::quit_`, `loopThread_` |
| Lock discipline: copy under the lock, call outside it | `EventLoop::run`, `ListenerTable::dispatch`, `RpcClient::State::onReadable` |
| `shared_ptr` for genuinely shared ownership; `weak_ptr` so a callback can't outlive its owner | `RpcClient::connect` (loop callback holds `weak_ptr<State>`) |
| Callback interface as an abstract class vs `std::function` for one-off callbacks | `IPropertyListener` vs `EventLoop::Task`, `PropertyManager::RawCallback` |
| Lambdas capturing by value / `this` / init-capture | `subscribe()` in `property_manager.h`, `tick` in `vehicle_service_main.cpp` |
| Small template: type-safe key | `PropertyKey<T>`, `PropertyManager::get/set/subscribe` |
| `std::get_if` for checked variant access | `PropertyManager::get`, `subscribe` |
| Member order for lifetimes (thread after loop; proxy before loop) | `LoopThread`, `PropertyManager` |
| RAII for threads and fds; idempotent `stop()` | `LoopThread`, `SignalFd` |
| Deleted copy ops on types that own OS resources | `EventLoop`, `RpcServer`, `SignalFd` |
| Private constructor + static factory returning `Result` | `RpcClient::connect`, `PropertyManager::connect` |

## 4. Mapping to real AOSP

| Here | AOSP |
|---|---|
| `EventLoop` (epoll + eventfd + timer map) | `Looper` (epoll + wake pipe); `post/postDelayed` ~ `Handler::post/postDelayed` |
| `LoopThread` | `HandlerThread` |
| `SignalFd` | `init` uses `signalfd`/`sigchld` handling the same way |
| `IPropertyListener` + `subscribe` | `ICarPropertyEventListener` registered with the car service |
| `PropertyManager` | `CarPropertyManager` (`registerCallback`, `getProperty`, `setProperty`) |
| `PropertyKey<T>` | `VehiclePropertyIds` + `CarPropertyValue<T>` casts |
| Event frame on the same socket | oneway Binder transaction into the client's callback binder |
| `onClientGone` | `linkToDeath` / `binderDied` on the listener binder |
| HAL change callback | `IVehicleCallback::onPropertyEvent` |

Where AOSP is more complex: subscription sample rates and change modes (on-change vs
continuous), per-area properties, callbacks delivered by Binder thread pools rather than
a single looper, listeners as Binder objects (so they can die independently of the
process), batching of events, and permission checks per property.

## 5. Design decisions and trade-offs

- **One loop thread per process; callbacks never overlap.** Servers are then lock-free
  (service, stub, HAL all touched from one thread). Cost: a slow callback delays everything else.
  Alternative: a worker thread pool (stage 6 territory).
- **The loop is in `common/`, not `framework/`.** `ipc` needs it and must not depend on
  the framework layer above; AOSP's `Looper` also lives in `libutils`, below Binder.
- **Events share the connection with calls.** Simplest possible push channel. The client
  therefore needs a reader thread that routes frames: replies to the blocked caller, events to a handler.
  Alternative: a second connection dedicated to events (no demultiplexing, but two sockets per client and awkward lifecycle).
- **One call in flight per `RpcClient`** (callers are serialised by a mutex). Matches the
  simple reply routing (no call ids). Trade-off: concurrent callers queue.
- **`transact()` refuses to run on the loop thread** (`IllegalState`) instead of deadlocking:
  the reply can only be read by the very thread that would be blocked. Same rule as
  "don't make a blocking Binder call on the thread that delivers your callbacks".
- **A timed-out call kills the connection** (5 s). Without call ids, a late reply could be
  taken for the next call's reply; closing is the safe answer.
- **`weak_ptr` in the loop callback** instead of making `removeFd` wait for a running
  callback. Cheaper to reason about; the price is one extra atomic per event.
- **Only *changes* produce events** (the HAL compares old and new). Simple rule, and it keeps `set(x, same)` quiet.
- **HAL ABI bumped to 2.** Adding virtual methods changes the vtable, so old plugins must be
  refused. The ABI check from stage 1 is now demonstrably useful.
- **Typed keys are a convenience layer; the untyped API stays** for tools that only learn
  types at runtime (the CLI).
- **Assumption:** `PropertyManager` keeps one connection and one loop thread per manager
  object, created by `connect()`.

### Locking design (threads + TSan)

| State | Touched by | Protection |
|---|---|---|
| `EventLoop` fd map, task queue, timers | any thread (add/post) + loop thread | `EventLoop::mutex_`; callbacks run *outside* the lock |
| `RpcServer`, `VehicleStub`, `VehicleService`, HAL | loop thread only | none needed (single-threaded by construction) |
| `RpcClient::State` reply slot, `dead`, handler | caller threads + loop thread | `State::mu` + condition variable |
| `RpcClient` socket write side | the one caller holding `callMutex` | `callMutex` |
| `RpcClient` socket read side + input buffer | loop thread only | none |
| `ListenerTable` (proxy) | subscribing threads + loop thread | `ListenerTable::mu`; listeners called outside it |
| `PropertyManager::subscriptions_` | any thread | `PropertyManager::mutex_` |

Rules that make it work: (1) copy shared data under the lock and call user code
after releasing it, so callbacks can (un)subscribe without deadlocking; (2) each object has exactly one
"home thread" or one mutex, never a mixture; (3) the loop outlives everything registered in it.
`build-tsan` (`-DMINI_AOSP_SANITIZER=thread`) runs the whole suite, including 4 threads hammering one `PropertyManager` while the service thread ticks.

## 6. Interview questions

1. **What is a Looper/Handler and why does Android use it?** A thread that blocks on epoll
   and processes a queue of messages/callbacks serially; other threads `post` work to it.
   It gives thread confinement instead of locks (the UI thread is one). `Handler` targets a Looper; `MessageQueue` holds timed messages.
2. **How does another thread wake an epoll loop?** Write to an eventfd (or pipe) that is registered in the epoll set; `epoll_wait` returns and the loop drains the posted tasks. Also needed when a new timer is earlier than the current wait timeout.
3. **epoll vs poll/select?** epoll keeps the interest set in the kernel (O(ready) per wait instead of O(watched)), and supports edge/level triggering. For a handful of fds the difference is small; the API is also cleaner for add/remove while running.
4. **Level-triggered vs edge-triggered?** Level: keeps firing while data remains (used here, so a partial read is fine). Edge: fires once per transition; you must drain until `EAGAIN` or you lose wake-ups.
5. **How do you avoid deadlock when a callback calls back into the same object?** Never hold a lock while calling user code; and refuse (or hand off) synchronous calls made from the thread that must deliver the reply (`IllegalState` here).
6. **Why `weak_ptr` in the epoll callback?** The callback may run after the owner is destroyed (another thread removed the fd while an event was already dispatched). `weak_ptr::lock()` turns "is it still alive?" into a safe check.
7. **What does TSan catch and what does it not?** Data races on memory actually accessed in the run (plus some lock-order inversions); it misses races on paths the test never executes, and logical races (bad protocol ordering) that aren't data races.
8. **oneway vs synchronous Binder calls, and how do callbacks fit?** Sync blocks until the reply. Oneway returns immediately and is queued in order per node; callbacks from a service to a client are usually oneway so a hung client cannot block the service. Our events are the same idea: the service never waits for the client.
9. **What happens to a subscription when the client dies?** Without cleanup the service leaks the listener and keeps writing to a dead socket. `onClientGone` (here) / `linkToDeath` (Binder) removes it.

## 7. Exercises

1. **Watch it live.** Run servicemanager + vehicle_service, then `vehicle_cli watch speed` in one
   terminal and `vehicle_cli set gear 3` in another. Expected: `speed = 5, 10, 15 ... 60`, one line every 200 ms, then silence. `set gear 0`: speed counts back down to 0.
2. **Add a property with events.** Add `PropertyId::Ignition` (bool, rw) in the HAL, plus a `PropertyKey<bool> kIgnition`. Subscribe in a tiny app and toggle it with the CLI. Expected: a callback with `true`/`false`. No IPC code needs to change - why not?
3. **Break the reply/event routing.** In `RpcClient::State::onReadable`, treat every frame as a reply. Run `framework_tests`. Expected: the subscribe test hangs or fails (events are swallowed as replies) - this shows why frames carry a type.
4. **Provoke the deadlock guard.** Remove the `inLoopThread()` check in `RpcClient::transact` and run `CallingBackIntoTheManagerFromACallbackIsRefused`. Expected: the test hangs until the 5 s timeout, then returns `Timeout` - the check turns a hang into an immediate error.
5. **See a data race.** Remove the `lock_guard` in `ListenerTable::add`, build `build-tsan` and run `framework_tests`. Expected: TSan reports a data race between the subscribing thread and the loop thread.
6. **Kill a subscribed client.** `vehicle_cli watch speed` in the background, `kill -9` it while the car accelerates. Expected: the service keeps running and logs nothing bad (`onClientGone` ran); `vehicle_cli get gear` still works.

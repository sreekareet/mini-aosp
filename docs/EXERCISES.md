# Advanced exercises

Optional, for after you have worked through the five stages. Roughly ordered by difficulty within each section.
Each lists the expected result so you know when you are done. Per-stage exercises live in `docs/stages/stage-N.md`.

## Stage 6 (not built): concurrency deepening

Stage 6 was optional in the brief and is described here as exercises. The project is single-loop-thread by
design (stages 3-5), which is simple and TSan-clean; these exercises step beyond it.

1. **Thread-safe message queue.** Write `common/blocking_queue.h`: `BlockingQueue<T>` with `push(T)`, `std::optional<T> pop()`
   (blocks), `tryPop()`, and `close()` (wakes all waiters; `pop()` then returns `nullopt` once drained). Use `mutex` + `condition_variable`.
   *Expected:* a test with 4 producers and 4 consumers moves 100k integers with none lost or duplicated, passes under `build-tsan`,
   and `close()` wakes a blocked consumer.
2. **Worker thread pool.** Write `common/thread_pool.h` on top of the queue: `submit(std::function<void()>)`, joins in the destructor.
   *Expected:* 8 tasks that each sleep 100 ms finish in about 200 ms on a 4-thread pool; destroying the pool with queued work runs or drops it
   (pick one and document why).
3. **Run slow subscriber callbacks off the loop.** Change `PropertyManager` so subscription callbacks are posted to a pool (per-subscription
   ordering preserved!) instead of running on the loop thread. *Expected:* a callback that sleeps 1 s no longer delays replies to `get()`;
   a callback may now call `pm.get()` without `IllegalState`. Think about what happens to ordering with more than one worker (hint: one
   serial executor per subscription).
4. **Multiple server threads.** Give `RpcServer` a pool for `onTransact` so a slow service method does not block others. *Expected:* two clients
   calling a deliberately slow method run in parallel. You must now protect `VehicleService`/HAL state - decide between one lock and
   per-object confinement, and record the trade-off in `docs/DESIGN.md`.
5. **Deadlock hunt.** Deliberately create a lock-order inversion between two mutexes, find it with TSan, then fix it with
   `std::scoped_lock` (or a documented order). *Expected:* TSan names both lock sites before the fix and stays silent after.
6. **`std::jthread` and `std::stop_token`** (C++20): port `LoopThread` and the reconnect worker to `std::jthread` with cooperative
   cancellation instead of a `stopping_` flag. *Expected:* same tests pass, less shutdown code.

## IPC and framework

7. **Call ids and concurrent calls.** Add an id to Call/Reply frames so `RpcClient` can have several calls in flight and match
   replies. *Expected:* 8 threads calling a slow method finish in roughly the time of one call; remove the `callMutex` serialisation.
8. **Oneway calls.** Add `transactOneway(code, data)` (no reply frame) and use it for `unsubscribe`. *Expected:* returns immediately; the
   server processes it in order with later calls.
9. **Pass a file descriptor.** Extend `Connection` to send an fd with `SCM_RIGHTS` and add `Parcel::writeFd/readFd`. Use it so the client
   passes a pipe for the service to write property updates into. *Expected:* data written by the service is readable in the client with no
   socket framing; both processes' fd counts return to normal after disconnect (check `/proc/<pid>/fd`).
10. **A tiny "AIDL" generator.** Write a Python script that reads a 10-line interface description and emits the interface, proxy and stub
    for one method set (the brief forbids generators inside the project, so keep it as a separate learning tool outside `CMakeLists`).
    *Expected:* the generated code reproduces `services/vehicle/vehicle_iface.*` for `getProperty/setProperty`.
11. **Sample rates.** Add `subscribe(id, listener, minIntervalMs)` and have the service throttle events per subscription (`on-change` vs
    `continuous` in real VHAL). *Expected:* a 1000 ms subscription to `speed` sees about one event per second while the car accelerates.
12. **Per-property permissions.** Replace the single writer allow-list with a table `PropertyId -> UidAllowList` for read and write.
    *Expected:* uid A may set `hvac_temp` but not `gear`; uid B may only read `speed`.

## Init and properties

13. **`on property:` triggers.** `on property:sys.foo=1` followed by `start <service>` in `init.conf` (see stage-4 exercise 5).
14. **`class` support.** `class core` on services plus `class_start core` / `class_stop core` driven by properties.
15. **Persistent properties.** Store `persist.*` in a file and reload them at boot. *Expected:* `setprop persist.x 1`, restart init, `getprop persist.x` still gives 1.
16. **Shared-memory property reads.** Replace the read path of `SystemProperties::get` with an `mmap`ed, read-only area that init updates
    (seqlock or version counter to avoid torn reads). *Expected:* `getprop` needs no socket; a reader never sees a half-written value under TSan.
17. **Reap orphans as PID 1.** Run init in a PID namespace (`unshare --pid --fork`) and make it reap children it did not spawn. *Expected:* no zombies after killing a service's children.

## HAL

18. **A C ABI HAL.** Redefine the plugin boundary as a C function table (`struct vehicle_hal_v1 { int (*get)(...); ... }`) so the loader and plugin no longer
    share C++ types. *Expected:* the plugin can be built with a different standard library/compiler flags and still load.
19. **Out-of-process HAL.** Run the fake HAL as its own process behind the same IPC as the vehicle service (the "binderized HAL" model).
    *Expected:* `kill -9` on the HAL no longer takes the vehicle service down, and the service reports `DeadObject` for HAL calls until it reconnects.
20. **Property areas.** Add a per-seat/per-window "area id" to properties (`(PropertyId, areaId)` keys) as real VHAL has. *Expected:* `hvac_temp` for the driver and passenger hold different values.

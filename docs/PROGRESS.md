# Progress

## Status

| Stage | State |
|---|---|
| 1. HAL interface, fake HAL `.so`, `dlopen` loader | done |
| 2. IPC: ServiceManager, Parcel, proxy/stub | done |
| 3. Framework manager, epoll event loop | done |
| 4. Mini init and property store | done |
| 5. Death notification, reconnect, `SO_PEERCRED` | next |
| 6. Optional concurrency deepening | not started (exercises only) |

## Build, run, test

```
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/bin/hal_demo

# multi-process demo (stage 2)
export MINI_AOSP_RUN_DIR=$(mktemp -d)
./build/bin/servicemanager & ./build/bin/vehicle_service &
./build/bin/vehicle_cli list
./build/bin/vehicle_cli set gear 3
./build/bin/vehicle_cli watch speed &     # then, in another terminal or after: 
./build/bin/vehicle_cli set gear 3        # speed = 5, 10, 15, ...
kill %1 %2 %3

# whole system under init (stage 4)
export MINI_AOSP_RUN_DIR=$(mktemp -d)
./build/bin/init &                           # starts servicemanager + vehicle_service from build/etc/init.conf
./build/bin/getprop                          # init.svc.*, sys.boot_completed, ...
./build/bin/setprop sys.demo hello
kill -9 $(./build/bin/getprop init.svc_debug_pid.vehicle_service)   # init restarts it
kill %1                                      # SIGTERM: orderly shutdown

# sanitizers (all tests pass under both)
cmake -S . -B build-asan -DMINI_AOSP_SANITIZER=address && cmake --build build-asan -j
cmake -S . -B build-tsan -DMINI_AOSP_SANITIZER=thread  && cmake --build build-tsan -j
ctest --test-dir build-tsan --output-on-failure
```

## Where to resume

Start Stage 5: (a) death notification - `IRemote::linkToDeath`, implemented in `RpcClient`
when the loop sees EOF/HUP; `PropertyManager` reconnect worker thread with backoff that re-looks-up the
service and re-subscribes existing listeners, plus a connection-state callback. (b) `SO_PEERCRED`:
`Connection::peerCredentials()`, `CallerInfo{uid,pid}` filled in `RpcServer`, permission policy in the vehicle stub
(writes) and property stub (`ro.`/`init.svc.` never via IPC, others by uid) and ServiceManager (`addService`).
Then bump `ro.mini_aosp.stage` in `init/init.conf`, write `stage-5.md`, `docs/EXERCISES.md`, final README pass.
Update this file at the end of each stage.

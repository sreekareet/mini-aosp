# Progress

## Status

| Stage | State |
|---|---|
| 1. HAL interface, fake HAL `.so`, `dlopen` loader | done |
| 2. IPC: ServiceManager, Parcel, proxy/stub | done |
| 3. Framework manager, epoll event loop | done |
| 4. Mini init and property store | next |
| 5. Death notification, reconnect, `SO_PEERCRED` | not started |
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

# sanitizers (all tests pass under both)
cmake -S . -B build-asan -DMINI_AOSP_SANITIZER=address && cmake --build build-asan -j
cmake -S . -B build-tsan -DMINI_AOSP_SANITIZER=thread  && cmake --build build-tsan -j
ctest --test-dir build-tsan --output-on-failure
```

## Where to resume

Start Stage 4: `init/` (config parser, service supervisor using `EventLoop` + `SignalFd`
for SIGCHLD, restart with crash-loop limit), `PropertyStore` + property service (hosted in init, served
with `RpcServer`), `getprop`/`setprop` apps, `init.conf`. `vehicle_service` already retries the
ServiceManager, so init can start things in any order. Update this file at the end of each stage.

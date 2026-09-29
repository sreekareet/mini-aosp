# Progress

## Status

| Stage | State |
|---|---|
| 1. HAL interface, fake HAL `.so`, `dlopen` loader | done |
| 2. IPC: ServiceManager, Parcel, proxy/stub | done |
| 3. Framework manager, epoll event loop | next |
| 4. Mini init and property store | not started |
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
kill %1 %2

# sanitizers
cmake -S . -B build-asan -DMINI_AOSP_SANITIZER=address && cmake --build build-asan -j
```

## Where to resume

Start Stage 3: `common/event_loop.*` (epoll + eventfd + timers), move `RpcServer` onto it
(replacing the `poll()` loop and `common/stop_signal.h`), add server-to-client events and
`subscribe`, `framework/PropertyManager`, HAL change callbacks, TSan build option.
Update this file at the end of each stage.

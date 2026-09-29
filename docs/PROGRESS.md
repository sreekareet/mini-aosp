# Progress

## Status

| Stage | State |
|---|---|
| 1. HAL interface, fake HAL `.so`, `dlopen` loader | done |
| 2. IPC: ServiceManager, Parcel, proxy/stub | next |
| 3. Framework manager, epoll event loop | not started |
| 4. Mini init and property store | not started |
| 5. Death notification, reconnect, `SO_PEERCRED` | not started |
| 6. Optional concurrency deepening | not started (exercises only) |

## Build, run, test

```
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/bin/hal_demo

# sanitizers
cmake -S . -B build-asan -DMINI_AOSP_SANITIZER=address && cmake --build build-asan -j
```

## Where to resume

Start Stage 2: `common/unique_fd.h`, `ipc/` (Parcel, transport, ServiceManager,
proxy/stub), `services/vehicle/`, `apps/vehicle_cli`. Update this file at the end of each stage.

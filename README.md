# mini-aosp

A small AOSP-style layered system in C++17 on plain Linux, built as a **learning
project** to practise Android platform concepts (HAL, Binder-style IPC, system
services, init, properties, Looper/Handler) and modern C++. It is not production
code and not a claim of production experience.

The theme is a fake Vehicle HAL (speed, fuel, gear, HVAC) exposed through a service
to small client apps.

## Architecture (as of stage 4)

```
   init        init: init.conf, service supervisor (restart, crash-loop limit), property service
                  | starts / restarts                 getprop / setprop -> SystemProperties
   apps        vehicle_cli (list/get/set/watch)     hal_demo (stage-1 demo, no IPC)
                  |  PropertyManager: get/set/subscribe, PropertyKey<T>, loop thread
   framework      |
                  |  VehicleServiceProxy ("Bp") over RpcClient (calls + pushed events)
   IPC         Parcel - frames - Unix sockets ---- servicemanager (name -> socket)
                  |  VehicleStub ("Bn") on RpcServer, driven by EventLoop (epoll)
   service     vehicle_service: VehicleService (subscriptions, 200 ms HAL tick)
                  |  IVehicleHal (get/set/callback/poll)
   HAL         LoadedHal (dlopen)  <-- libfake_vehicle_hal.so   (the "vendor" plugin)
```

The finished system (stages 1-5) will look like this; this diagram is filled in as
stages land:

```
 apps ── framework (PropertyManager) ── IPC (Unix sockets, Parcel, proxy/stub)
                                              │
              init ── servicemanager ── vehicle service ── HAL (.so)
```

## What maps to what in AOSP

| Layer | AOSP equivalent | This project |
|---|---|---|
| Apps | Apps | `vehicle_cli` |
| Framework | Managers (car property manager) | `PropertyManager` + listeners (**stage 3**) |
| Services | system_server / CarService | `vehicle_service` process (**stage 2**, subscriptions **stage 3**) |
| IPC | Binder + ServiceManager | Unix sockets, Parcel, proxy/stub (**stage 2**) |
| HAL | HIDL/AIDL HALs | `IVehicleHal`, `.so` via `dlopen` (**stage 1**) |
| Looper/Handler | Looper, HandlerThread | `EventLoop`, `LoopThread` (**stage 3**) |
| Init | init + init.rc | `init`, `init.conf`, `Supervisor` (**stage 4**) |
| Properties | system properties, `getprop`/`setprop` | `PropertyStore` + property service in init, `SystemProperties` (**stage 4**) |

## Build, run, test

```
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
./build/bin/hal_demo

# multi-process demo
export MINI_AOSP_RUN_DIR=$(mktemp -d)
./build/bin/servicemanager & ./build/bin/vehicle_service &
./build/bin/vehicle_cli list
./build/bin/vehicle_cli watch speed 5 &   # prints changes for 5 seconds
./build/bin/vehicle_cli set gear 3         # the car accelerates: speed = 5, 10, 15...
kill %1 %2

# or let init run everything (restarts crashed services)
export MINI_AOSP_RUN_DIR=$(mktemp -d)
./build/bin/init &
./build/bin/getprop
kill -9 $(./build/bin/getprop init.svc_debug_pid.vehicle_service)
./build/bin/vehicle_cli get gear      # works again after init restarts the service
kill %1                               # orderly shutdown
```

Options: `-DMINI_AOSP_SANITIZER=address` (ASan+UBSan) or `thread` (TSan), `-DMINI_AOSP_WERROR=OFF`,
`-DMINI_AOSP_SYSTEM_GTEST=ON` (use `libgtest-dev` if the GoogleTest download fails).

## Docs

- `docs/PROGRESS.md` - status and where to resume
- `docs/DESIGN.md` - decisions and trade-offs
- `docs/stages/stage-N.md` - study material per stage
- `CLAUDE.md` - the project brief

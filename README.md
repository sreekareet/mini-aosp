# mini-aosp

A small AOSP-style layered system in C++17 on plain Linux, built as a **learning
project** to practise Android platform concepts (HAL, Binder-style IPC, system
services, init, properties, Looper/Handler) and modern C++. It is not production
code and it is not a claim of production experience.

The theme is a fake Vehicle HAL (speed, fuel, gear, HVAC) exposed through a service
to small client apps, which matches an Android Automotive background.

Start with `docs/PROGRESS.md`, then read `docs/stages/stage-1.md` ... `stage-5.md`
in order. Each stage has a reading order, interview questions and hands-on exercises.

## Architecture

```
 init         init: reads init.conf, starts + restarts services (crash-loop limit),
                    hosts the property service (getprop / setprop)
                 | spawns / supervises
 apps         vehicle_cli (list/get/set/watch)   getprop / setprop   hal_demo
                 |                                  |
 framework    PropertyManager                    SystemProperties
              typed PropertyKey<T>, subscribe,
              death detection + auto-reconnect
                 | VehicleServiceProxy ("Bp")
                 |   RpcClient: calls + pushed events, loop thread, linkToDeath
 IPC          Parcel - framed Unix sockets - EventLoop (epoll) - SO_PEERCRED identity
                 |                                  servicemanager: name -> socket
                 | VehicleStub ("Bn")               (registration restricted by uid)
 service      vehicle_service: VehicleService (subscriptions, permission check on writes)
                 | IVehicleHal (get / set / change callback / poll)
 HAL          LoadedHal (dlopen)  <-  libfake_vehicle_hal.so   (the "vendor" plugin)
```

Processes at runtime: `init` -> `servicemanager`, `vehicle_service`; clients connect over
sockets in `/tmp/mini-aosp-<uid>/` (override with `MINI_AOSP_RUN_DIR`).

## What maps to what in AOSP

| Layer | AOSP equivalent | This project |
|---|---|---|
| Apps | Apps | `vehicle_cli`, `getprop`, `setprop` |
| Framework | Managers (car property manager), `SystemProperties` | `PropertyManager` + listeners, `SystemProperties` |
| Services | system_server / CarService | `vehicle_service` process |
| IPC | Binder + ServiceManager | Unix sockets, `Parcel`, proxy/stub, `servicemanager` |
| Identity / permissions | `getCallingUid`, `checkCallingPermission` | `SO_PEERCRED` + `UidAllowList` |
| Death handling | `linkToDeath` / `binderDied` | `IRemote::linkToDeath`, `PropertyManager` reconnect |
| Looper/Handler | `Looper`, `HandlerThread` | `EventLoop`, `LoopThread` |
| HAL | HIDL/AIDL HALs (passthrough era: `dlopen`) | `IVehicleHal`, `.so` via `dlopen` |
| Init | init + init.rc | `init`, `init.conf`, `Supervisor` |
| Properties | system properties, property service | `PropertyStore` + property service in init |

Each stage doc says where real AOSP is more complex.

## Build, run, test

Requirements: g++ (C++17), CMake 3.16+, git. GoogleTest v1.17.0 is downloaded by CMake
(use `-DMINI_AOSP_SYSTEM_GTEST=ON` with `libgtest-dev` if you are offline).

```
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run the whole system under `init`:

```
export MINI_AOSP_RUN_DIR=$(mktemp -d)
./build/bin/init &                                  # starts servicemanager + vehicle_service
./build/bin/getprop                                 # init.svc.*, sys.boot_completed, ro.*
./build/bin/vehicle_cli list
./build/bin/vehicle_cli watch speed 10 &            # prints changes for 10 s
./build/bin/vehicle_cli set gear 3                  # the car accelerates: speed = 5, 10, 15 ...
kill -9 $(./build/bin/getprop init.svc_debug_pid.vehicle_service)   # crash the service
                                                    # watch shows [service died] / [service back]
kill %1                                             # SIGTERM: init stops everything in order
```

Or start the pieces by hand: `./build/bin/servicemanager &  ./build/bin/vehicle_service &`.
The stage-1 demo needs no IPC: `./build/bin/hal_demo`.

Options: `-DMINI_AOSP_SANITIZER=address` (ASan+UBSan) or `thread` (TSan; use a separate
build directory, e.g. `cmake -S . -B build-tsan -DMINI_AOSP_SANITIZER=thread`),
`-DMINI_AOSP_WERROR=OFF` (warnings are errors by default).

Tests: one GoogleTest executable per layer (`hal_tests`, `common_tests`, `ipc_tests`, `service_tests`,
`framework_tests`, `init_tests`). They include end-to-end tests that spawn the real binaries in a temporary
run directory, and all pass under normal, ASan/UBSan and TSan builds.

## Repository layout

```
common/      Result/Status, UniqueFd, EventLoop, SignalFd, logging, paths (header-mostly "libutils")
hal/         IVehicleHal, fake HAL plugin (.so), dlopen loader
ipc/         Parcel, framing, wire format, RpcServer/RpcClient, ServiceManager, access policy
services/    vehicle service: interface + proxy, implementation + stub, service process
framework/   PropertyManager, SystemProperties
init/        init, config parser, Supervisor, PropertyStore + property service
apps/        CLI clients
tests/       GoogleTest suites and helpers
docs/        PROGRESS.md, DESIGN.md, EXERCISES.md, stages/stage-N.md
```

## Docs

- `docs/PROGRESS.md` - status and where to resume
- `docs/DESIGN.md` - one entry per stage: decision, alternatives, trade-off
- `docs/stages/stage-N.md` - study material per stage
- `docs/EXERCISES.md` - advanced exercises, including the optional concurrency stage
- `CLAUDE.md` - the project brief

# Progress

## Status

| Stage | State |
|---|---|
| 1. HAL interface, fake HAL `.so`, `dlopen` loader | done |
| 2. IPC: ServiceManager, Parcel, proxy/stub | done |
| 3. Framework manager, epoll event loop | done |
| 4. Mini init and property store | done |
| 5. Death notification, reconnect, `SO_PEERCRED` | done |
| 6. Optional concurrency deepening | not built - described as exercises in `docs/EXERCISES.md` |

All six test suites pass in normal, ASan/UBSan and TSan builds, with `-Wall -Wextra -Wpedantic -Werror`.

## Build, run, test

```
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure

# sanitizers (use separate build directories)
cmake -S . -B build-asan -DMINI_AOSP_SANITIZER=address && cmake --build build-asan -j && ctest --test-dir build-asan
cmake -S . -B build-tsan -DMINI_AOSP_SANITIZER=thread  && cmake --build build-tsan -j && ctest --test-dir build-tsan
```

Stage 1 demo (single process, no IPC):

```
./build/bin/hal_demo
```

The whole system under init (stages 2-5):

```
export MINI_AOSP_RUN_DIR=$(mktemp -d)            # isolated socket directory
./build/bin/init &                               # starts servicemanager + vehicle_service (build/etc/init.conf)
./build/bin/getprop                              # init.svc.*, sys.boot_completed, ro.*
./build/bin/setprop sys.demo hello && ./build/bin/getprop sys.demo
./build/bin/vehicle_cli list
./build/bin/vehicle_cli watch speed 10 &         # stream changes for 10 seconds
./build/bin/vehicle_cli set gear 3               # the car accelerates: speed = 5, 10, 15 ...
kill -9 $(./build/bin/getprop init.svc_debug_pid.vehicle_service)   # init restarts it; watch prints
                                                 # [service died] / [service back] and carries on
kill %1                                          # SIGTERM to init: orderly shutdown of everything
```

Without init: `./build/bin/servicemanager &` then `./build/bin/vehicle_service &` (same env var), and use the CLIs.

## Where to resume

Stages 1-5 are complete, committed and documented. Nothing is half-done.

Suggested next steps for the owner:
1. Read `docs/stages/stage-1.md` ... `stage-5.md` in order (each has a reading order, interview questions and exercises).
2. Work through the per-stage exercises, then `docs/EXERCISES.md` (which also describes the optional Stage 6, concurrency
   deepening: blocking queue, thread pool, TSan-clean).
3. If you extend the code, keep the rule from `CLAUDE.md`: one stage at a time, tests first, commit as `stage N: ...`, update these docs.

Known gaps (also listed in `docs/DESIGN.md`): ServiceManager restart does not trigger re-registration; the cross-uid
permission path is tested via an allow-list rather than a real second user (no passwordless sudo in the dev environment);
`init` is not PID 1; a HAL crash takes the whole vehicle service down (passthrough HAL).

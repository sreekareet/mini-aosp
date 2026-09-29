# mini-aosp

A small, AOSP-style layered system written in C++ on plain Linux. It is a learning and interview-portfolio project.

## How this project is being built (read first)

The owner has limited time with Claude Code. So Claude Code writes the code now, and the owner studies, runs, and extends it later, stage by stage. This means:

- Build the stages in order, autonomously. Do not stop for approval between steps. If a decision is genuinely ambiguous, pick the simplest option, record the assumption in `docs/DESIGN.md`, and continue.
- Each stage must build cleanly and pass its tests before the next one starts. Commit each stage separately with the message `stage N: <summary>` so the git history shows the stages.
- The code is the lesson. Write clear, simple code with short "why" comments at the key concept points (not line-by-line narration), and produce the study material described in "Study material" for every stage.
- Keep `docs/PROGRESS.md` current: what is done, what is next, how to build, run and test, and exactly where to resume. If usage or context is running low, stop at a clean stage boundary (everything committed, docs written) instead of stopping mid-stage.

## Environment

Ubuntu on WSL2, g++ 15, CMake 4.x, git (identity already configured). The project lives in the Linux home directory. Use `cmake_minimum_required(VERSION 3.16)` or higher. Get GoogleTest through CMake `FetchContent`, pinned to a release tag; fall back to `libgtest-dev` if the download fails.

## Goals (in priority order)

1. Hands-on AOSP platform concepts (HAL, Binder-style IPC, system services, init, properties, Looper/Handler) so the owner can discuss them confidently in Android platform interviews.
2. Strengthen and demonstrate solid modern C++ (see "C++ focus").
3. Show design thinking: clear layers, small interfaces, a recorded reason for each decision.

The theme is a fake Vehicle HAL (speed, fuel level, gear, etc.) exposed through a service to small client apps. This matches the owner's Android Automotive background.

## Layers and their AOSP equivalents

| Layer | AOSP equivalent | This project |
|---|---|---|
| Apps | Apps | Small CLI clients |
| Framework | Managers (e.g. car property manager) | Client library with a `PropertyManager` and listeners |
| Services | system_server / CarService | One service process hosting a few services |
| IPC | Binder + ServiceManager | Unix domain sockets, a simple Parcel, proxy/stub classes |
| HAL | HIDL/AIDL HALs | Abstract `IVehicleHal`, implementations loaded as `.so` via `dlopen` |
| Init | init + init.rc | Tiny init that reads a config file and restarts crashed services |

## Keep it simple (important)

- Do NOT over-engineer. Interfaces stay small: a few methods, obvious names, no speculative extension points.
- No heavy frameworks or external dependencies. Standard library, CMake, and GoogleTest only.
- Build one stage at a time, in order. Do not mix later-stage features into an earlier stage; leave a short `// TODO(stage N)` instead.
- Prefer the simplest design that still shows the AOSP idea, even if real AOSP does it in a more complex way. Note the difference in the study material instead of building it.
- No code generators, macro-heavy tricks, or clever metaprogramming.
- The whole project should stay small enough that the owner can read and understand every file in a few hours per stage.

## C++ focus

Use this project to practise and show good C++. Use these where they fit naturally, and point them out in comments and study material. Do not force them in.

- RAII everywhere: file descriptors, sockets, and `dlopen` handles wrapped in small owning types. No naked `new`/`delete`.
- Ownership: `unique_ptr` by default, `shared_ptr`/`weak_ptr` only where ownership is really shared (the stand-in for AOSP `sp`/`wp`).
- Rule of zero/five, move semantics, const-correctness.
- Value and sum types: `std::optional`, `std::variant`, `std::string_view`, `enum class`.
- Explicit error handling. Pick one style per layer (result/error-code types, or exceptions) and document the choice in `docs/DESIGN.md`.
- Callbacks with `std::function` and lambdas; abstract interfaces with virtual destructors; PIMPL where it helps ABI stability at the HAL boundary.
- Small templates are welcome (for example a type-safe property key). Keep them readable.
- Baseline is C++17. Use C++20 only where a stage clearly benefits (`std::span`, `std::jthread`).

Concurrency is introduced late on purpose. Stages 1-2 are single-threaded. Threads arrive with the event loop and callbacks (stage 3). Add ThreadSanitizer to the build once threads exist and explain the locking design in the study material.

## Build and quality

- CMake with one target per layer, warnings on (`-Wall -Wextra -Wpedantic`), and `-Werror` once the build is clean.
- An option to build with ASan/UBSan (and TSan once threads exist).
- A few GoogleTest tests per layer: one happy path, one failure or edge case. Keep tests short.
- Always run the build and tests yourself and fix failures before committing a stage.

## Repo layout

```
CMakeLists.txt
CLAUDE.md
README.md            architecture diagram + "what maps to what in AOSP" + build/run steps
docs/PROGRESS.md     status and resume point
docs/DESIGN.md       one entry per stage: decision, alternatives, trade-off
docs/stages/         stage-1.md, stage-2.md, ... (study material)
docs/EXERCISES.md    optional advanced exercises for later
hal/                 IVehicleHal, fake HAL plugin (.so), loader
ipc/                 Parcel, socket transport, ServiceManager, proxy/stub
framework/           PropertyManager, event loop
services/            vehicle service
init/                mini init
apps/                CLI clients
tests/
```

## Stages

1. HAL interface, fake HAL `.so`, `dlopen` loader, single process.
2. IPC: ServiceManager, Parcel, proxy/stub over Unix sockets.
3. Framework manager with `subscribe()` callbacks and an epoll-based event loop (first threads).
4. Mini init and a property store.
5. Robustness: death notification with reconnect, and `SO_PEERCRED` permission checks.
6. Optional, only if there is room: concurrency deepening (thread-safe message queue, worker thread pool, TSan-clean). If not built, describe it as exercises in `docs/EXERCISES.md`.

Build stages 1-5 now. Do not start stage 6 unless everything before it is finished, committed, and documented.

## Study material (required for every stage)

Create `docs/stages/stage-N.md` containing:

1. What was built and why (short).
2. Reading order: which files to read, in what order.
3. C++ concepts used, with the files where each one appears.
4. How this maps to real AOSP, and where real AOSP is more complex.
5. Design decisions and trade-offs (also summarised in `docs/DESIGN.md`).
6. 5-8 likely interview questions with concise answers.
7. 3-5 hands-on exercises the owner can try (for example: add a new property, kill the service on purpose and observe what happens), each with the expected outcome.

Also keep `README.md` up to date with the architecture diagram, layer mapping table, and how to build, run, and test.

## Rules

- Write original code. Do not copy AOSP source. Describe the AOSP concept and build a small version in this project's own structure.
- Do not claim or imply this project is production experience. The docs describe it honestly as a learning project.
- If something would make the design significantly more complex, choose the simpler option and note the alternative in the study material.

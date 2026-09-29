# Stage 1: HAL interface, fake HAL `.so`, `dlopen` loader

## 1. What was built and why

- `IVehicleHal`: the abstract contract for "the car" (list / get / set properties).
- `libfake_vehicle_hal.so`: a vendor-style plugin implementing it with fake data.
- `LoadedHal` / `DlHandle`: the platform-side loader (`dlopen`/`dlsym`) in RAII types.
- `hal_demo`: single-process demo. `hal_tests`: GoogleTest suite.

Why: a HAL exists so the platform never links against vendor code. The vendor ships
a library, the platform loads it at runtime by path. Everything above the HAL only
sees the interface.

## 2. Reading order

1. `common/result.h` - `Status` and `Result<T>`, used everywhere.
2. `hal/vehicle_hal.h` - property ids, `PropValue` variant, `IVehicleHal`, plugin ABI.
3. `hal/fake_vehicle_hal.cpp` - the plugin and its three `extern "C"` entry points.
4. `hal/hal_loader.h` then `.cpp` - `DlHandle`, `LoadedHal` (read the lifetime comment).
5. `apps/hal_demo.cpp`, then `tests/hal_test.cpp`.

## 3. C++ concepts used

| Concept | Where |
|---|---|
| RAII for a `dlopen` handle, move-only type | `DlHandle` in `hal/hal_loader.*` |
| Member declaration order = destruction order | `LoadedHal` (`lib_` before `hal_`) |
| Deleting a special member on purpose (rule of five) | `LoadedHal::operator=` |
| `unique_ptr` with a custom deleter (function pointer) | `LoadedHal::hal_` |
| `std::variant`, `std::get_if`, `std::visit`-free dispatch | `PropValue` in `vehicle_hal.h` |
| `std::optional` | `parsePropertyId`, `parseValue`, `numeric()` in the fake HAL |
| `enum class` | `PropertyId`, `ValueType`, `Status` |
| `std::string_view`, `std::from_chars` | `parseValue` |
| Abstract interface + virtual destructor | `IVehicleHal` |
| `extern "C"` and function-pointer casts | plugin entry points, `LoadedHal::load` |
| Error codes via a small result type | `common/result.h` |
| const-correctness | `get()`/`listProperties()` are `const`; `hal()` has const overload |

## 4. Mapping to real AOSP

| Here | AOSP |
|---|---|
| `IVehicleHal` | the Vehicle HAL AIDL interface (`IVehicle`) |
| `.so` + `dlopen` | Legacy "passthrough" HALs did exactly this (`hw_get_module`, `HAL_MODULE_INFO_SYM`). Modern HALs are separate processes reached over Binder. |
| ABI version symbol | `HAL_MODULE_INFO_SYM` carries a version tag; AIDL has stable, versioned interfaces |
| `PropertyId` enum | `VehicleProperty` ids, with area ids (per-seat / per-window) and access/change-mode flags |

Where AOSP is more complex: area ids, property status/timestamps, `subscribe` with
sample rates, VHAL running in its own process with SELinux policy. We skip all of it.

## 5. Design decisions and trade-offs

- **C++ virtual interface across the `.so` boundary.** Simple and readable, but it
  requires the same compiler and standard library on both sides (vtable layout,
  `std::string` layout). Alternatives: a pure C function-table ABI (robust, clumsy),
  PIMPL with a C API (stable ABI, more code), or a separate process (what AOSP
  chose). Recorded in `docs/DESIGN.md`.
- **Result/error codes, no exceptions**, matching Android native code.
- **Three exported C symbols** (`abi version`, `create`, `destroy`). The plugin
  allocates and frees its own object, so allocator/runtime mismatches cannot bite.
- **Deleted move-assignment on `LoadedHal`.** The defaulted one would `dlclose`
  the library before destroying the old HAL object.

## 6. Interview questions

1. **What is a HAL and why does Android have one?** A stable interface between the
   hardware-independent framework and vendor-specific implementations, so the
   platform can be updated without rewriting drivers (and vice versa - Treble).
2. **Passthrough vs binderized HAL?** Passthrough: HAL library loaded into the
   client process via `dlopen`. Binderized: HAL runs in its own process, accessed
   over Binder/HIDL/AIDL. Binderized gives isolation, SELinux boundaries, and
   versioned stable interfaces. This stage is the passthrough model.
3. **Why `extern "C"` on plugin entry points?** To avoid C++ name mangling so
   `dlsym` can look the symbol up by its plain name.
4. **Why must the `.so` outlive the objects created from it?** Their vtables and
   destructors are code inside the library. `dlclose` first means calling into
   unmapped memory. Hence declaration order in `LoadedHal`.
5. **What is wrong with passing `std::string`/vtable classes across a plugin
   boundary?** The layout isn't a stable ABI: different compiler or stdlib
   versions break it silently. Fix with a C ABI, PIMPL, or IPC.
6. **`RTLD_NOW` vs `RTLD_LAZY`?** NOW resolves every symbol at load time so a broken
   plugin fails immediately; LAZY defers to first call and can crash mid-run.
7. **Why `enum class` and `std::variant` for properties?** Type safety: no silent
   int conversions, and a value can only be one of the listed types.
8. **Why a separate `destroy` function instead of `delete` in the loader?** The
   object should be freed by the same runtime that allocated it.

## 7. Exercises

1. **Add a property.** Add `PropertyId::Ignition` (bool, writable) to `vehicle_hal.h`
   (also `toString`, `kAllPropertyIds`) and `fake_vehicle_hal.cpp`. Run `hal_demo`:
   a fifth row appears.
2. **Second HAL.** Copy the fake HAL into `hal/other_hal.cpp` with different initial
   values, add a `SHARED` target, and pass its path: `./build/bin/hal_demo build/lib/libother.so`.
   The demo output changes without rebuilding the demo - that is the point of a plugin.
3. **Break the ABI on purpose.** Change `kVehicleHalAbiVersion` to 2 in the header, rebuild
   only `hal_demo` (`touch` it, `make hal_demo`) and run with the old `.so`.
   Expected: `has ABI 1, expected 2` and exit code 1.
4. **Break the lifetime rule.** In `LoadedHal`, swap the member declaration order
   (`hal_` first) and run `hal_tests` under ASan (`-DMINI_AOSP_SANITIZER=address`).
   Expected: a crash / use-after-unmap when the object is destroyed.
5. **Missing symbol.** Run `hal_demo libm.so.6`. Expected: "missing symbols" and a clean failure.

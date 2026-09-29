# Stage 2: IPC - ServiceManager, Parcel, proxy/stub over Unix sockets

## 1. What was built and why

- `Parcel`: typed byte buffer (the message body).
- `Connection` / `Listener`: length-prefixed frames over Unix stream sockets.
- Wire format (`wire.h`): Call and Reply frames with a status code.
- `IRemote` (client side, "IBinder::transact") and `IStub` (server side, "onTransact").
- `RpcServer`: a single-threaded `poll()` loop serving many clients.
- `ServiceManager`: name -> socket path registry, as its own process.
- Vehicle service: `IVehicleService` interface, `VehicleServiceProxy` (Bp),
  `VehicleStub` (Bn), `VehicleService` (real impl on the HAL), `vehicle_service` process.
- `vehicle_cli`: app that finds the service by name and calls it.

Why: Binder is the backbone of Android. The point is the *shape* - a caller uses a
plain C++ interface, a proxy turns calls into parcels, a stub turns parcels back into
calls, and a name service connects the two sides - not the kernel driver.

## 2. Reading order

1. `ipc/parcel.h` / `.cpp` - the data container and its error model.
2. `ipc/wire.h` / `.cpp` - what a Call and a Reply look like on the wire.
3. `ipc/transport.h` / `.cpp` - framing, non-blocking reads, `Listener`.
4. `ipc/remote.h`, `ipc/stub.h` - the two halves of a transaction.
5. `ipc/rpc_server.cpp` - the server loop.
6. `ipc/service_manager.*` and `ipc/servicemanager_main.cpp`.
7. `services/vehicle/vehicle_iface.*` (interface + proxy), then `vehicle_service.*` (stub + impl), then `vehicle_service_main.cpp`.
8. `apps/vehicle_cli.cpp`, then `tests/` (`test_util.h` first; `LoopbackRemote` is a neat trick).

## 3. C++ concepts used

| Concept | Where |
|---|---|
| RAII for fds (`UniqueFd`), sockets, and the socket *path* (`Listener` unlinks in its destructor) | `common/unique_fd.h`, `ipc/transport.*` |
| Move-only types, `std::exchange` in move constructors | `UniqueFd`, `Listener`, `Connection` |
| Rule of five done deliberately (assignment deleted where it would be wrong) | `Listener` |
| `unique_ptr` ownership of polymorphic objects | `SocketRemote` held by proxies |
| Abstract interfaces + virtual dtors; same interface for proxy and real impl | `IVehicleService`, `IRemote`, `IStub` |
| `std::variant` serialisation with an explicit tag | `writeValue` / `readValue` |
| `enum class`, `std::optional`, `string_view` | `FrameType`, `parsePropertyId`, `Parcel::writeString` |
| Sticky-error reads (like `std::istream`) | `Parcel` |
| `Result<T>` / `Status` error handling end to end | everywhere |
| Structured bindings in range-for | `RpcServer::serveOnce` |
| Defensive parsing of untrusted input | `Parcel::readString`, `Connection::takeFrame` |

## 4. Mapping to real AOSP

| Here | AOSP |
|---|---|
| `IVehicleService` | an AIDL interface |
| `VehicleServiceProxy` / `VehicleStub` | generated `BpXxx` / `BnXxx` |
| `IRemote::transact` / `IStub::onTransact` | `IBinder::transact` / `BBinder::onTransact` |
| `Parcel` | `android::Parcel` |
| `ServiceManager` (returns a socket path) | `servicemanager` (returns a `IBinder` handle) |
| socket path as "address" | Binder handle, kernel-mediated |
| `poll()` loop | binder thread pool (`IPCThreadState::joinThreadPool`) |

Where AOSP is more complex: the kernel driver copies data once and enforces identity
(uid/pid) itself; objects and file descriptors can be sent inside a Parcel; a
Binder handle *is* a capability (you can only call what you were handed); there are
thread pools, oneway calls, death recipients, and reference counting across
processes. Here, "the address is a path" and every transaction is synchronous.
Death notification and identity arrive in stage 5.

## 5. Design decisions and trade-offs

- **Unix stream sockets + our own framing** rather than Binder ioctl. Portable, easy
  to debug (`strace`, `socat`), and shows framing explicitly. Cost: two copies, no kernel-enforced identity (added via `SO_PEERCRED` in stage 5).
- **ServiceManager returns a socket path**, and the client connects straight to the
  service. Simplest thing that keeps "lookup by name, then direct calls".
  Alternative: the ServiceManager proxies all traffic (simpler for clients, but a bottleneck).
- **Sticky-error `Parcel`** instead of returning a `Result` from every read. Far less
  boilerplate in proxy/stub code; one `ok()` check per method.
- **Status travels in the Reply frame**, so callee errors (`NotFound`, `ReadOnly`)
  reach the caller unchanged - like `binder::Status`.
- **Proxy and real implementation share one C++ interface**, so a caller (or a test) cannot tell them apart. `LoopbackRemote` exploits this to test marshalling with no sockets.
- **Non-blocking server with per-connection input buffers**, so a client that sends half a frame cannot stall everyone else.
- **Sockets are world-connectable (0666)**; access control is meant to be uid checks in the service (stage 5), mirroring how the Binder node is world-accessible.
- **`poll()` now, epoll in stage 3.** Stage 2 is single-threaded by design; the server loop is replaced by the shared event loop next stage.
- **Host byte order** in Parcels (same machine). A network protocol would pin endianness.

## 6. Interview questions

1. **What is Binder and why not just use sockets?** Kernel IPC with one-copy transfer,
   kernel-verified caller uid/pid, object references and fds in messages, death
   notification, and a thread pool model. Sockets give none of the identity or object semantics for free.
2. **Explain proxy/stub.** The proxy implements the interface on the client by marshalling arguments into a Parcel and calling `transact`; the stub unmarshals and calls the real implementation. AIDL generates both.
3. **What does ServiceManager do?** Maps names to service handles; every process can look up, only privileged ones may register (SELinux `add` permission). It is handle 0, the one address everyone knows.
4. **What is a Parcel?** A flat serialisation buffer for a transaction; reads and writes must be in the same order on both sides, so a versioning/compat story matters (AIDL is stable-by-design, hand-written parcelables aren't).
5. **Why length-prefixed frames?** A stream socket has no message boundaries; a length header lets the receiver reassemble frames from arbitrary chunks.
6. **How do you defend against a malicious/buggy peer?** Bound the frame size, validate every length against remaining bytes, never trust counts, fail closed (sticky error), and drop the connection on protocol violations.
7. **Sync vs oneway calls?** Sync blocks the caller until the reply (and can deadlock on cycles); oneway is fire-and-forget and queued per-node. This stage is sync only; stage 3 adds server-to-client events which behave like oneway.
8. **What happens if the service dies mid-call?** The caller sees a transport error (`DeadObject`). Binder returns `DEAD_OBJECT`/`DeadObjectException` and delivers `linkToDeath` callbacks; stage 5 builds that.

## 7. Exercises

1. **Add a method.** Add a `kGetAll = 4` transaction that returns every property's value in one call. Touch `vehicle_iface.h`, the proxy, the stub, and add a test using `LoopbackRemote`. Expected: `service_tests` passes; `vehicle_cli` unchanged.
2. **Watch the wire.** Run `strace -f -e trace=sendto,recvfrom,send,recv ./build/bin/vehicle_cli get gear` (with the service running) and find the frame: 4-byte length then Call (0), code (1), property id (3). Expected: 12-byte payload.
3. **Kill the service on purpose.** Start servicemanager and vehicle_service, `kill -9` the service, run `vehicle_cli get gear`. Expected: `cannot reach vehicle service: DeadObject` (stale socket file, connection refused). A graceful `kill` (SIGTERM) instead removes the socket, giving `NotFound`. Why the difference?
4. **Register a second service.** Write a tiny `echo_service` (stub with one method that returns its string argument) and a client that finds it via `getServiceRemote`. Expected: both services appear in the ServiceManager list.
5. **Corrupt a frame.** With `socat`/`python3`, connect to the vehicle socket and send `ffffffff` as a length. Expected: the connection is dropped; the service keeps serving others.

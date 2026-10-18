# NX-MCP Agent Guide

## C++ Style

This project follows [Google's C++ Style Guide](https://google.github.io/styleguide/cppguide.html). Key conventions:

- Types (classes, structs, enums): `PascalCase`
- Functions and methods: `PascalCase`
- Local variables and parameters: `snake_case`
- Non-static member variables: `snake_case_` (trailing underscore)
- Constants (`constexpr`, `const`, enumerators): `kCamelCase`
- Namespaces: `lowercase`
- Indentation: 2 spaces

## Project Structure

### `server/`

Contains the MCP (Model Context Protocol) server, which runs as a Nintendo Switch sysmodule. The architecture is split into three layers:

- **HTTP layer** (`server/http/`) — custom synchronous BSD-socket TCP server (`socket_server.cpp`). `App` owns the `SocketServer` and routes `/mcp` and `/ping` through the normal controller dispatch loop, and `/fs/` directly to `FileTransferController` before body buffering occurs. This is the only layer that knows about HTTP.
- **Controllers** (`server/controllers/`) — one class per route group. Each controller parses the HTTP request, delegates to a service, and writes the HTTP response. No business logic lives here. Controllers that stream large responses (e.g. `FileTransferController`) may write directly to the raw socket via `req.fd` and return `Response::Raw()` (status = 0) to suppress the normal `SendResponse` call in `HandleConnection`.
- **Services** (`server/services/`) — business logic. Services have no knowledge of HTTP; they receive plain data and return plain data.
- **Tools** (`server/tools/`) — MCP tools. Each tool inherits from `tools::Tool` and self-registers into `ToolRegistry` via a static initializer in its `.cpp`. The service calls tools polymorphically through the registry with no direct knowledge of individual tools.

JSON serialization uses `nlohmann/json` (`vendor/json`).

Protocol version strings live in `server/mcp_versions.hpp` (`mcp::kVersion = "2026-07-28"`, `mcp::kLegacyVersion = "2025-11-25"`). Do not hardcode version strings elsewhere.

The entrypoint (`server/main.cpp`) uses the standard libnx sysmodule pattern: `__appInit` initializes services (`smInitialize`, `setsysInitialize` to set the host version, `socketInitialize`, `fsInitialize`/`fsdevMountSdmc`), then the main loop runs `App::Run()`. `__appExit` tears down in reverse order: `fsdevUnmountAll`/`fsExit`, `socketExit`, `smExit`. sm is kept open for the module lifetime so tools can call service init functions at runtime.

Build with `make` from the `server/` directory. The output is `server/exefs.nsp`. The server listens on port **57331**.

#### IPC Service

`server/ipc/` implements a minimal CMIF server that runs on a dedicated thread alongside the HTTP server.

`ipc::StartIpcThread()` is called from `main` before `app.Run()`. It registers the `nx:mcp` system service with `sm` and then loops on `svcReplyAndReceive`, accepting up to 4 concurrent sessions.

Only one command is handled:

| Command ID | Name | Returns |
|---|---|---|
| `0` | `GetVersion` | `mcp::Version` (`mcp::kServerVersionInfo`) |

This is the mechanism the launcher uses to check whether the module is running and what version it reports (`launcher/source/version.cpp` queries `nx:mcp` command 0 via `serviceDispatchOut`).

#### MCP Protocol

The server is **dual-era**, supporting both:

- **Modern** (`2026-07-28`) — stateless, per-request `_meta`
- **Legacy** (`2025-11-25`) — session-based `initialize` handshake (used by Claude Code)

**Era detection** in `McpController::HandlePost`: if `params._meta` contains `io.modelcontextprotocol/protocolVersion`, the request is routed to `HandleModernRequest`; otherwise to `HandleLegacyRequest`. Requests with no `id` are notifications and receive HTTP 204 with no body.

- **Modern path**: validates `_meta.protocolVersion == mcp::kVersion` and presence of `clientCapabilities`. Wrong version returns JSON-RPC error `-32022` (`UnsupportedProtocolVersionError`).
- **Legacy path**: no `_meta` required. `initialize` validates `params.protocolVersion == mcp::kLegacyVersion`.

All HTTP responses use status **200**, including JSON-RPC errors. The only exceptions are 204 (notifications) and 405 (GET on `/mcp`).

The MCP endpoint is `POST /mcp`; `GET /mcp` returns 405.

#### File Transfer

Two streaming endpoints live under `/fs/`. They bypass the body-buffering path in `App` so files of any size are handled with a single 64 KB stack buffer:

| Method | Path | Description |
|---|---|---|
| `GET` | `/fs/download?path=<sdmc_path>` | Stream a file from the SD card to the client |
| `PUT` | `/fs/upload?path=<sdmc_path>` | Stream a file from the client to the SD card |

`FileTransferController` captures `req.fd` in lambdas passed to `FileTransferService`. `on_ready(size)` writes the HTTP response headers directly to the socket; `on_chunk(data, n)` / `on_recv(buf, max)` stream the body. The controller returns `Response::Raw()` when done so `HandleConnection` skips `SendResponse`.

#### Tools

Tools inherit from `tools::Tool` and return `tools::ToolResult`:

```cpp
struct ToolResult {
  std::string text;
  bool is_error = false;
  std::string image_data;  // non-empty → return image content instead of text
  std::string image_mime;
};
```

Set `is_error = true` for tool-level errors (bad arguments, resource not found, etc.). `McpService` propagates this as `isError` in the MCP result content block. If `image_data` is non-empty, `McpService` emits an `image` content block (`data` + `mimeType`) instead of a `text` block. Unhandled exceptions thrown by `Execute` are caught by `McpService` and returned as `is_error = true` results — they do not crash the server.

#### Adding a Tool

1. Create `server/tools/my_tool.hpp` — subclass `tools::Tool`, declare `Name`, `Description`, `InputSchema`, `Execute`.
2. Create `server/tools/my_tool.cpp` — implement the methods and add:
   ```cpp
   static bool kRegistered =
       tools::ToolRegistry::Instance().Register(std::make_unique<MyTool>());
   ```

The Makefile auto-discovers all `.cpp` files in `server/tools/` — no Makefile change needed. Multiple tool classes can share one `.cpp` (e.g. `hid_input_tools.cpp` contains four classes that share internal helpers).

### `server/tests/`

Tests use **Catch2 v2** (`vendor/catch2/catch.hpp`). Run with `make run` from `server/tests/`.

Each test binary is self-contained — it compiles only the translation units it needs. Tool auto-registrations are excluded so the registry starts empty. `ToolRegistry::Clear()` is called in a fixture to reset state between test cases.

#### Directory structure

```
server/tests/
  Makefile
  controllers/   — test_<controller>.cpp for each controller
  services/      — test_<service>.cpp and mock_<service>.cpp
  tools/         — test_<tool_or_registry>.cpp
```

#### Mock services

Services that depend on `switch.h` (i.e. `FileTransferService`) cannot be compiled into host tests. Instead, a `mock_<service>.cpp` in `tests/services/` provides a test-only implementation of the same class without the Switch SDK dependency. The mock stores call arguments in `namespace mock` globals and returns a configurable status. Controller tests `extern`-declare those globals to inspect and configure them.

Services with no Switch SDK dependency (e.g. `McpService`) are tested directly using the real implementation.

#### Adding tests

- **Controller tests**: add `test_<controller>.cpp` under `tests/controllers/`. If the controller's service depends on `switch.h`, also add `mock_<service>.cpp` under `tests/services/`. Add a new `run_<name>_tests` target to the Makefile linking the test file, the controller `.cpp`, and the mock (or real) service `.cpp`.
- **Service tests**: add `test_<service>.cpp` under `tests/services/`. Link the real service `.cpp` and any dependencies (e.g. `tool_registry.cpp`).
- **Tool tests**: add `test_<tool>.cpp` under `tests/tools/`. Link the tool's `.cpp` alongside `tool_registry.cpp`.

### `launcher/`

A Nintendo Switch homebrew app (NRO) built with the **Arriba** UI framework (`vendor/Arriba`). It acts as a front-end for installing and managing the NX-MCP sysmodule.

**Startup flow:**
1. Checks for the sysmodule NSP at `sdmc:/atmosphere/contents/0100000000057331/exefs.nsp`.
2. If absent, shows an install prompt that copies `exefs.nsp` from the launcher's own RomFS (bundled at build time).
3. Once installed, shows the main UI.

**Main UI** — three buttons driven by DPad + A:
- **Restart Module** — terminates then re-launches the sysmodule via `pmshell`.
- **Kill Module** — terminates the sysmodule without re-launching.
- **Toggle Boot2 Flag** — creates or removes `sdmc:/atmosphere/contents/0100000000057331/flags/boot2.flag`, which controls whether Atmosphere auto-starts the module on boot.

The screen also shows the console's local IP, the MCP port (`57331`), and upload port (`7332`).

**Upload server** — listens on TCP port `7332` for a raw HTTP PUT/POST carrying a new `exefs.nsp`. On successful receive it writes the file to the SD card path and calls `ModuleRestart`. This enables iterative development without re-running the launcher install flow.

**Build:**
```
make            # from launcher/ — builds launcher.nro
make clean      # removes build/, romfs/, and output binaries
```

The `prepare-romfs` Makefile target runs first: it copies Arriba's GLSL shaders into `romfs/`, then builds the server (`make` in `../server/`) and copies `server/exefs.nsp` into `romfs/exefs.nsp` so the launcher can bundle and install it.

### `vendor/`

Git submodules / vendored headers for third-party dependencies:

- `vendor/json` — single-header JSON library (`single_include/nlohmann/json.hpp`)
- `vendor/catch2` — git submodule, Catch2 v2.13.10 (`single_include/catch2/catch.hpp`)

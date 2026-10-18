# NX-MCP

A Nintendo Switch [sysmodule](https://switchbrew.org/wiki/Title_list/System_modules) that hosts a [Model Context Protocol (MCP)](https://modelcontextprotocol.io/) server, letting an LLM control and inspect the console remotely over WiFi.

The primary use case is **game and software development**: deploy builds to the SD card, launch and kill titles, inspect running processes, manipulate save data, send controller input to drive test scenarios, and capture screenshots - all from the same AI coding session producing the code. Connect Claude Code (or any MCP client) to your Switch and close the loop between writing code and seeing it run on hardware.

With the context NX-MCP provides, AI agents can autonomously pull crash reports, analyse the stack trace and register state, cross-reference your source and map file, and suggest a fix.

## Installation

1. Download `launcher.nro` from the [latest release](https://github.com/CompSciOrBust/NX-MCP/releases/latest).
2. Copy it to your Switch's SD card under `switch/`.
3. Launch it and press **Install**.
4. Optionally press **Toggle Boot2 Flag** to enable auto-start on boot.

## Connecting an MCP client

The server listens on **port 57331** and supports both the modern (`2026-07-28`) and legacy (`2025-11-25`) protocol versions.

For Claude Code:
```
claude mcp add --transport http nx-mcp http://<switch-ip>:57331/mcp
```

For Codex:
```
codex mcp add nx-mcp --url http://<switch-ip>:57331/mcp
```

For other tools, ask the AI how to adapt the above for itself.

## Available Tools

### Input
| Tool | Description |
|------|-------------|
| `hid_press_buttons` | Inject virtual controller input — hold buttons/sticks for a duration |
| `hid_sequence` | Execute a multi-step input sequence in a single call |
| `hid_touch` | Tap the touchscreen at given coordinates (1280×720, supports multi-touch) |
| `hid_swipe` | Drag across the screen from one point to another |

### Screenshot
| Tool | Description |
|------|-------------|
| `screenshot` | Capture a JPEG screenshot of the Switch display, returned as an image |

### Filesystem
| Tool | Description |
|------|-------------|
| `fs_list_dir` | List directory contents on the SD card |
| `fs_read_file` | Read text file content (up to 64 KB per call) |
| `fs_write_file` | Write UTF-8 text to a file, overwriting existing content |
| `fs_delete` | Delete a file or empty directory |
| `fs_mkdir` | Create a directory |
| `fs_crc32` | Compute the CRC32 checksum of a file using hardware acceleration |

### Processes
| Tool | Description |
|------|-------------|
| `pm_list_processes` | All running processes with PIDs, program IDs, and memory pool usage |
| `pm_get_process_info` | Program ID and role for a process by PID |
| `pm_kill` | Terminate a process by PID or program ID |
| `pm_launch` | Launch a program by program ID |

### Save Data
| Tool | Description |
|------|-------------|
| `list_installed_games` | All installed titles and game cards with title IDs and names |
| `list_save_data` | Save data containers, filterable by type (`account`, `device`, `bcat`) and/or title ID |
| `mount_save` | Mount a save container so it's accessible via the `fs_*` tools |
| `unmount_save` | Unmount a previously mounted save container |

### IPC
| Tool | Description |
|------|-------------|
| `ipc_call` | Send a raw CMIF IPC command to any named system service — supply inline data, send/receive buffer descriptors, and optional output handle slots; returns hex-encoded inline output, buffer contents, and handle values |
| `ipc_close` | Close a kernel session handle previously obtained from `ipc_call` `out_handles` |

### System
| Tool | Description |
|------|-------------|
| `get_system_info` | Firmware version, Atmosphère version, emuMMC status, IP address, system tick |
| `list_accounts` | All user accounts with UIDs and nicknames |
| `power` | Reboot or shut down the console |

## File Transfer

The `fs_read_file` and `fs_write_file` tools pass file contents through the MCP response, which loads them into the LLM's context. For large files — binaries, assets, compiled builds — use these plain HTTP endpoints instead to transfer data directly without consuming context:

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/fs/download?path=<sdmc_path>` | Stream a file from the SD card |
| `PUT` | `/fs/upload?path=<sdmc_path>` | Stream a file to the SD card |

## Launcher

The launcher NRO is a homebrew front-end for managing the sysmodule. It shows the console's IP and MCP port, and provides three actions:

- **Restart Module** — terminate and re-launch the sysmodule
- **Kill Module** — terminate without re-launching
- **Toggle Boot2 Flag** — control whether the module auto-starts on boot

It also hosts an upload server on port **7332** for pushing sysmodule updates over the air (see [Building from Source](#building-from-source)).

## Building from Source

Requires [devkitPro](https://devkitpro.org/) with devkitARM and libnx.

```sh
git clone --recurse-submodules https://github.com/CompSciOrBust/NX-MCP
cd launcher && make
```

The output is `launcher/launcher.nro`. Building the launcher also builds `server/exefs.nsp` and bundles it inside the NRO.

### Updating the sysmodule over the air

After rebuilding (`cd server && make`), push the new NSP to a running Switch without re-running the launcher:

```sh
curl -s -X POST http://<switch-ip>:7332 --data-binary @server/exefs.nsp && echo "upload OK"
```

The launcher's upload server receives the file and restarts the module automatically.

## Project Structure

```
server/          — MCP sysmodule (builds to exefs.nsp)
  main.cpp       — sysmodule entry point
  http/          — custom synchronous BSD-socket HTTP server
  controllers/   — HTTP request routing
  services/      — business logic
  tools/         — MCP tool implementations
  ipc/           — CMIF IPC service (nx:mcp, used by the launcher)
launcher/        — Homebrew NRO front-end
vendor/          — Git submodules
```

## Adding a Tool

1. Create `server/tools/my_tool.hpp` — subclass `tools::Tool`, declare `Name`, `Description`, `InputSchema`, `Execute`.
2. Create `server/tools/my_tool.cpp` — implement the methods and self-register:
   ```cpp
   static bool kRegistered =
       tools::ToolRegistry::Instance().Register(std::make_unique<MyTool>());
   ```
3. Rebuild — the Makefile auto-discovers all `.cpp` files in `server/tools/`.

## Running Tests

```sh
cd server/tests && make run
```

Tests use Catch2 v2. Each test file compiles only the translation units it needs; `ToolRegistry::Clear()` resets state between cases.

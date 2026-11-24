# Privacy audit

## Claim

The shipped `tapir.exe` has no network access, no telemetry, no service, no scheduled task, and no updater. This document records the code evidence for that claim.

## Evidence

### Network and telemetry

A search of `src\` for `winsock`, `ws2tcpip`, `winhttp`, `wininet`, `urlmon`, `curl`, `ws2_32`, `socket`, `InternetOpen`, and `WinHttpOpen` returns no matches. No source file includes a network header or calls a network API.

`CMakeLists.txt:85` lists the linked libraries: `user32`, `shell32`, `ole32`, `oleaut32`, `shlwapi`, `propsys`, `shcore`, `crypt32`, `advapi32`, `d3d11`, `dxgi`, `d2d1`, `dwrite`, `dwmapi`, `dcomp`, and `gdi32`. None is a network library. `CMakeLists.txt:84` adds `src` and `third_party` as include directories. `third_party\` contains `toml\toml.hpp` and `doctest\doctest.h` only.

The log is local and capped at 64 KiB by `kLogMaxBytes` in `src/diagnostics.h:9`. Writes carry reasons only: `src/config_file.cpp:64-71`, `src/util/history_store.cpp:109-120`, and `src/util/startup.cpp:96-101`. The `OutputDebugStringA` calls in `src/window.cpp:728-733` and `src/index.cpp:460-472` also write fixed reason strings. No log call includes clipboard text.

### Service, scheduled task, and updater

A search of `src\` and `resources\` for `CreateService`, `StartService`, and `schtasks` returns no matches. `resources/app.manifest:1-12` declares the assembly identity, `PerMonitorV2` DPI awareness, `longPathAware`, and the UTF-8 active code page. It has no `requestedExecutionLevel`.

tapir keeps one resident Win32 message loop by design. `src/main.cpp:61-89` waits with `MsgWaitForMultipleObjects` and dispatches with `PeekMessage`. A named mutex and a show event at `src/main.cpp:24-31` give single-instance behavior. The app index builds on an in-process thread at `src/index.cpp:376-401`. The config directory watcher at `src/util/config_path.cpp:50-64` uses `FindFirstChangeNotificationW`. None of these creates a service or a scheduled task.

### Outbound local interactions

The executable starts other processes only for local actions. `src/index.cpp:409-432` calls `ShellExecuteExW` or `IApplicationActivationManager::ActivateApplication` to launch an indexed app. `src/window.cpp:787-793` calls `explorer.exe` with `/select` for the Open file location action. `src/window.cpp:639-650` sends `Ctrl+V` with `SendInput` during the paste flow. These calls do not open a network connection.

The startup `Run` value is opt in. `src/util/startup.cpp:21-26` defines the HKCU `Run` path and the value name `tapir`. `src/util/startup.cpp:103-169` writes or deletes that value from `start_with_windows`. The default is `false` in `src/config.h:34`.

Clipboard history is protected with DPAPI. `src/util/history_store.cpp:122-147` wraps `CryptProtectData` and `CryptUnprotectData`. `src/util/history_store.cpp:190-218` writes to a temporary file and replaces `history.dat` atomically. The default is `encrypt_history = true` in `src/config.h:74`.

## Concerns

| Concern | Verdict | Evidence | Caveat |
|---|---|---|---|
| Network access | Absent | No network headers or calls in `src\`; no network library in `CMakeLists.txt:84` | None |
| Telemetry | Absent | No network code; local log capped at 64 KiB with reason-only writes | None |
| Service | Absent | No `CreateService` or `StartService` in `src\` or `resources\` | The resident process is intentional and is not a service |
| Scheduled task | Absent | No `schtasks` in `src\` or `resources\` | Windows may trigger the `Run` entry at sign-in when `start_with_windows` is enabled |
| Updater | Absent | No network code, service, or scheduled task | None |
| Clipboard data at rest | DPAPI-encrypted by default | `src/util/history_store.cpp:122-147` and `src/util/history_store.cpp:190-218` | `launches.dat` is unencrypted but holds only app keys and counts |
| Startup | Opt in | `src/util/startup.cpp:21-26` and `src/util/startup.cpp:103-169` | Windows starts tapir at sign-in when the `Run` value is present |

## How to recheck

Run these commands from the repository root. The first prints no matches. The second lists the imported DLLs of the Release executable, and none is a network library.

	Get-ChildItem -Path src -Recurse -File -Include *.cpp,*.h | Select-String -Pattern 'winhttp|wininet|ws2_32|socket|CreateService|schtasks|telemetry'
	Select-String -Path CMakeLists.txt -Pattern 'winhttp|wininet|ws2_32'

	dumpbin /imports build\Release\tapir.exe

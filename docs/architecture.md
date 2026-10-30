# Architecture

A static port of a Wii game to Android, and to Windows for fast iteration and debugging. The PowerPC code in `main.dol` is
translated to C++ at build time and compiled natively; no CPU emulator runs at run time. First target: **Sengoku Basara 2
Heroes** (`RBSJ08`).

The code base descends from WiiCompiled / NSMB-Wii-Multiplayer-Recompilation (GPLv3). Every active hook now uses Basara
addresses; `python -I tools/basara-audit.py` lists any that do not.

## Build flow

```
your own disc image
  └─ extracted to ──> assets/basara-heroes/sys/main.dol
                         │
                         ▼
translator/ (C#/.NET 8)  +  config/basara/{recomp.yml, MAP.txt}
                         │  PowerPC -> C++ per function, data sections -> blobs
                         ▼
generated/  (translated game code, never committed)
                         │
                         ▼
runtime/ (C++, HLE)  +  aurora/ (GX -> WebGPU/Dawn)
                         │
               ┌─────────┴─────────┐
               ▼                   ▼
        Windows .exe        Android APK (arm64, Vulkan 1.1, minSdk 30)
        tools/b-win.ps1     tools/b-android.ps1 + Gradle (app/)
```

## Components

| Path | Contents |
|---|---|
| `translator/` | The translator (`Translator.Core`, `Translator.Cli`) and its tests. Reads the DOL, the function map and the entry points from `recomp.yml`. |
| `config/basara/` | Game configuration: `recomp.yml` (memory, SDA `0x80667C80`, SDA2 `0x8066B100`, entry `0x8000403C`) and `MAP.txt` (function bounds). `config/examples/` holds a generic template. |
| `runtime/` | The host runtime: `main.cpp`, flat guest memory (`guest_flat_memory*.cpp`), guest threads as fibers (`fiber_manager.cpp`), PowerPC and paired-single helpers, input (`virtual_remote.cpp`, `wii_remote_input.cpp`), ImGui overlay (`settings_overlay.cpp`), system stats (`system_stats.cpp`), profiler (`guest_profiler.cpp`). |
| `runtime/src/hle/` | Replacements for Wii SDK functions: OS, threads and interrupts (`os/`), GX (`gx/`), AX/DSP audio (`audio/`), NAND, ISFS and DVD (`storage/`), IOS/IPC, SC, VI (`vi.cpp`: retrace and present). Basara-specific overrides are in `basara_*.cpp`. |
| `aurora/` | GX renderer on WebGPU (Dawn). Texture cache and content-keyed palette cache in `lib/gx/gx.cpp`. |
| `app/` | Android app (Gradle module). The root holds `build.gradle` and `settings.gradle`. |
| `tools/` | Build and run (`b-win.ps1`, `b-android.ps1`, `b-translate.ps1`, `basara.ps1`, `b-cycle.sh`), analysis (`w1.py`, `b-dis.py`, `b-sym.sh`, `b_remap.py`, `basara-audit.py`), live log (`b-logwindow.ps1`). `tools/build/` holds the CMake flags and the dependency fetcher. |

## Runtime model

- **Memory:** guest RAM is mapped flat (Windows: `0x100000000000 + address`) and read big-endian through helpers. Physical and uncached aliases map to the same block.
- **Threads:** each `OSThread` is a host fiber under a cooperative scheduler. A new thread enables interrupts before its entry point (`fiber_manager.cpp`).
- **Interrupts, alarms, retrace:** pumped from the guest's `OSRestoreInterrupts`, so a thread that busy-polls still lets the VI retrace and alarms through.
- **Hooks:** guest functions are replaced by HLE by address. Addresses and `r13`/`r2` (SDA) offsets must be Basara's.
- **Graphics:** GX commands -> aurora -> Dawn (D3D12 on Windows, Vulkan on Android). GX DrawSync and breakpoints are HLE.
- **Audio:** AX/DSP are HLE, with a host mixer (`audio_backend.cpp`).

## Diagnostics

- Log: `generated/b/console-live.txt`. Live window: `tools/b-logwindow.ps1`.
- Environment: `BASARA_LOG_FPS=1`, `BASARA_LOG_PALETTE=1`, `BASARA_PROFILE=255:75`.
- Memory write tracer: `tools/basara.ps1 -Watch ADDR:LEN`.
- "Show FPS" overlay: fps, CPU/GX ms, draws, CPU/GPU load, temperature, RAM, battery.

## Not in this repository

| Path | Why |
|---|---|
| `assets/` | Your own disc dump (copyrighted) |
| `generated/` | Translated game code, logs, build output |
| `third_party/` | Portable tools and dependency sources, fetched by `tools/setup-android.ps1` |
| `*.so`, `*.apk`, `*.exe` | Build products; binaries containing translated code must not be distributed |
| `*.dol`, `*.rel`, `*.iso`, `*.rvz` and others | Game files |

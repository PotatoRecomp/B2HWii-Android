# Sengoku Basara 2 Heroes for Android

An unofficial port of *Sengoku Basara 2 Heroes* (Wii, NTSC-J, `RBSJ08`) to Android (arm64) and Windows. The PowerPC code in
`main.dol` is statically recompiled to C++ on a PC, then built together with a high-level emulation runtime and a
GX-to-WebGPU renderer (Vulkan on Android). No CPU emulator runs on the device.

Based on [NSMB-Wii-Multiplayer-Recompilation](https://github.com/WitherBlitz/NSMB-Wii-Multiplayer-Recompilation)
and [WiiCompiled](https://github.com/patchzyy/Wiicompiled), both GPLv3. See [Credits](#credits).

Port made by Shiinobu, continuing the work of WitherBlitz with the help of WiiCompiled's code, and entirely coded by
Claude Opus 5.5.

> [!NOTE]
> This repository contains no game assets and no game code. You must provide a dump of your own legally acquired disc.
>
> This is an unofficial fan project, not affiliated with or endorsed by Capcom. Sengoku Basara, its characters and its logo
> belong to Capcom.

## Status

Work in progress. The title screen and the battle demo run at 60 fps on Windows. Testing on a phone (RedMagic 8S Pro) is
ongoing.

## Requirements

- To run: Android 11 (API 30) or newer, arm64, with a Vulkan 1.1 driver.
- To build: Windows, .NET 8, llvm-mingw in `third_party/llvm-mingw` (for the Windows build) and Android NDK 28.2 (see `tools/setup-android.ps1`).

## Layout

| Path | Contents |
|---|---|
| `app/` | Android app (Gradle module) |
| `runtime/` | Host runtime and Wii SDK HLE (OS, GX, AX, NAND/DVD, VI) |
| `aurora/` | GX renderer on WebGPU (Dawn) |
| `translator/` | PowerPC-to-C++ translator (C#/.NET 8) |
| `config/basara/` | Game configuration: `recomp.yml`, `MAP.txt` |
| `tools/` | Build, run and analysis scripts |
| `docs/architecture.md` | [Architecture](docs/architecture.md) |

## Build

```powershell
# Extract your disc so that assets/basara-heroes/sys/main.dol exists, then:
tools\setup-android.ps1               # toolchain and dependencies (third_party/)
tools\b-translate.ps1                 # PowerPC -> C++ (generated/)
tools\b-win.ps1 -Mode configure       # once
tools\b-win.ps1 -Mode build           # Windows
tools\b-android.ps1 -Mode configure   # once
tools\b-android.ps1 -Mode build
tools\b-android.ps1 -Mode stage       # libmain.so and data for the APK
third_party\gradle\bin\gradle.bat assembleRelease
```

## Credits

This project is a modified version of NSMB-Wii-Multiplayer-Recompilation, which is built on WiiCompiled. It was changed
from October 2026 to run *Sengoku Basara 2 Heroes*.

- **[NSMB-Wii-Multiplayer-Recompilation](https://github.com/WitherBlitz/NSMB-Wii-Multiplayer-Recompilation)** by
  WitherBlitz: the Android app and the runtime this port started from.
- **[WiiCompiled](https://github.com/patchzyy/Wiicompiled)** by patchzyy and contributors: the static recompiler and
  runtime underneath it.
- **[Jude (@YlPorts)](https://github.com/YlPorts)**: the Android draw-count and Mali GPU changes.
- **[KartPad](https://github.com/chrissotraidis/kartpad)**: WiiCompiled on Android, whose approach and game-file import
  steps this port follows.
- **[aurora](https://github.com/encounter/aurora)** by Luke Street: the GX rendering and windowing backend.
- **[Dawn](https://dawn.googlesource.com/dawn)**: Google's WebGPU implementation, used by aurora for Vulkan and
  Direct3D.
- **[Dolphin Emulator](https://github.com/dolphin-emu/dolphin)**: the reference for Wii hardware behavior, the free DSP
  coefficient ROM and the default WiiConnect24 tree in the runtime, and the tool you extract your game with.
- **The NSMBW reverse-engineering community**: RootCubed's
  [NSMBW symbols](https://github.com/RootCubed/nsmbw-symbols), the NSMBW decompilation and the NSMBW address maps. The
  NSMBW function map was the starting point for this game's map.
- **[DriftDroid](https://github.com/dorPXP/driftdroid)** and **[Galaxy-Android](https://github.com/OpenPort-Labs/Galaxy-Android)**: other Wii-to-Android
  ports, read for reference.

Bundled third-party components and their licenses are listed in [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).

## License

GPLv3, see [`LICENSE`](LICENSE) and [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md). Translated game code, and any
APK containing it, must not be distributed.

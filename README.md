# Sengoku Basara 2 Heroes for Android

<div align="justify">

An unofficial port of *Sengoku Basara 2 Heroes* (Wii, NTSC-J, `RBSJ08`) to Android (arm64) and Windows.

The game's PowerPC code is recompiled to C++ ahead of time on a PC and built into a native app, together with a runtime that stands in for the Wii's system software and a GX renderer on Vulkan. Nothing is emulated on the phone.

Port made by Shiinobu, continuing the work of WitherBlitz with the help of WiiCompiled's code, and entirely coded by Claude Opus 5.5. Based on [NSMB-Wii-Multiplayer-Recompilation](https://github.com/WitherBlitz/NSMB-Wii-Multiplayer-Recompilation) and [WiiCompiled](https://github.com/patchzyy/Wiicompiled), both GPLv3 (see [Credits](#credits)).

</div>

> [!NOTE]
> This repository contains no game assets and no game code. You need a dump of your own legally acquired disc.
>
> This is an unofficial fan project, not affiliated with or endorsed by Capcom. Sengoku Basara, its characters and its logo belong to Capcom.

<div align="justify">

**Contents:** [Status](#status) · [Features](#features) · [Getting started](#getting-started) · [Saves](#saves) · [Reporting a problem](#reporting-a-problem) · [Building](#building) · [Credits](#credits) · [License](#license)

## Status

**0.0.1 Beta 5** (November 2026). The whole game is playable on Android and Windows, from boot through Story battles, with sound, saving and loading.

| Class | Tested on | Battles |
|---|---|---|
| Flagship | RedMagic 8S Pro (Snapdragon 8 Gen 2) | 60 fps |
| Mid-range | Poco X7 Pro (Dimensity 8400), Infinix GT30 (Dimensity 7300), Redmi Note 12 Pro (Snapdragon 732G) | 35–60 fps with Frameskip |
| Low-end | Poco M3 and Redmi 9T (Snapdragon 662), Helio G81, Helio G99, Unisoc T606 | 35–60 fps with Frameskip |

The full change history is in the app (**What's New**) and on the [Releases](../../releases) page.

### New in Beta 5

- Upright launcher: Play and the settings in use on the first page, every setting in the tabs below.
- Settings in game: tap the gear at the top left (or press Back). Most settings apply at once.
- Custom GPU drivers (Turnip for Adreno, malisx2 for Mali) and a lighter Vulkan renderer.
- SGSR upscaling, and GX Thread (graphics on its own CPU core) now without flicker.
- Save import also takes a `data.bin` copied from a Wii or from Dolphin.
- Fixes: horse riding and boss crashes, Adreno 610 crash, voice repeats at 30 fps, crackling sound, slow touch camera.

## Features

| | |
|---|---|
| **Performance** | Frameskip, 30 FPS Lock, Dynamic Resolution, GX Thread. A device profile picks resolution and frameskip on first start. |
| **Graphics** | Render resolution 0.5×–4×, FSR1 and SGSR upscaling, Texture Detail (trilinear and anisotropic filtering), custom textures and texture dumping, Vulkan renderer choice, custom GPU drivers. |
| **Controls** | Touch controls with a layout editor and touch camera (with Invert Camera), gamepads in the Classic Controller layout, and on Windows a keyboard with rebindable keys. |
| **Launcher** | Imports ISO or RVZ on the phone, Continue and Stop, in-game pause menu, performance overlay, cheats, save backup and import, What's New, English and Indonesian. |
| **Experimental** | HD Textures Live (2× upscale while playing), Thermal Control, texture key reports. Each comes with a warning in Settings > Experimental. |

## Getting started

**Requirements:** Android 11 or newer, arm64, with Vulkan 1.1.

1. Install the APK from this repository's [Releases](../../releases) page. Do not use APKs from anywhere else.
2. Open the app and allow file access when asked.
3. Pick your own dump of the game (`.iso` or `.rvz`, Wii NTSC-J `RBSJ08`). The app imports it once.
4. Tap **Play**. The first start chooses settings for your phone; you can change them any time in the tabs.

Beta builds install next to the release build and over each other. Coming from the old 0.9.0 Beta, uninstall it once.

## Saves

The save lives in the app's own storage. In **Settings > Save Data**:

- **Back Up** writes `basara2heroes-save-DATE.zip` to `Basara2Heroes-Beta/Saves`. Copy it to a PC or cloud drive to keep it safe.
- **Import** replaces the save on the phone. It accepts:
  - a `.zip` made by Back Up,
  - a bare `basara2w.dat`,
  - a `data.bin` copied to SD from a Wii (Data Management) or exported by Dolphin (Export Wii Save).
- **Undo Last Import** brings back the save from before the last import.

## Reporting a problem

Each game start writes its own folder, `Basara2Heroes-Beta/Logs/<date and time>/`, with `log/console.log` and, after a crash, a report in `crash/`. Attach that folder, your phone model and the settings in use (shown at the top of the launcher) to your report.

## Building

For developers. Requires Windows, .NET 8, llvm-mingw in `third_party/llvm-mingw` (Windows build) and Android NDK 28.2 (fetched by `tools/setup-android.ps1`).

```powershell
# Extract your disc so that assets/basara-heroes/sys/main.dol exists, then:
tools\setup-android.ps1               # toolchain and dependencies (third_party/)
tools\b-translate.ps1                 # PowerPC -> C++ (generated/)
tools\b-win.ps1 -Mode configure       # once
tools\b-win.ps1 -Mode build           # Windows
tools\b-android.ps1 -Mode configure   # once
tools\b-android.ps1 -Mode build
tools\b-android.ps1 -Mode stage       # libmain.so and data for the APK
cd app
..\third_party\gradle\bin\gradle.bat assembleRelease   # add -PbasaraBeta -PbasaraBetaNumber=N for a Beta
```

Optional: `tools\b-pgo.ps1` for a profile-guided (PGO + ThinLTO) Android build.

| Path | Contents |
|---|---|
| `app/` | Android app (Gradle module): launcher, settings, touch controls |
| `runtime/` | Host runtime and Wii SDK HLE (OS, GX, AX, NAND/DVD, VI) |
| `aurora/` | GX renderer on WebGPU (Dawn) and Vulkan |
| `translator/` | PowerPC-to-C++ translator (C#/.NET 8) |
| `config/basara/` | Game configuration: `recomp.yml`, `MAP.txt` |
| `tools/` | Build, run and analysis scripts |

How the pieces fit together: [docs/architecture.md](docs/architecture.md).

## Credits

This project is a modified version of NSMB-Wii-Multiplayer-Recompilation, which is built on WiiCompiled. It was changed from October 2026 to run *Sengoku Basara 2 Heroes*.

- **[NSMB-Wii-Multiplayer-Recompilation](https://github.com/WitherBlitz/NSMB-Wii-Multiplayer-Recompilation)** by WitherBlitz: the Android app and the runtime this port started from.
- **[WiiCompiled](https://github.com/patchzyy/Wiicompiled)** by patchzyy and contributors: the static recompiler and runtime underneath it.
- **[Jude (@YlPorts)](https://github.com/YlPorts)**: the Android draw-count and Mali GPU changes.
- **[KartPad](https://github.com/chrissotraidis/kartpad)**: WiiCompiled on Android, whose approach and game-file import steps this port follows.
- **[aurora](https://github.com/encounter/aurora)** by Luke Street: the GX rendering and windowing backend.
- **[Dawn](https://dawn.googlesource.com/dawn)**: Google's WebGPU implementation, used by aurora for Vulkan and Direct3D.
- **[Dolphin Emulator](https://github.com/dolphin-emu/dolphin)**: the reference for Wii hardware behavior, the free DSP coefficient ROM and the default WiiConnect24 tree in the runtime, the RVZ format the launcher reads, and the Wii save (`data.bin`) format.
- **The NSMBW reverse-engineering community**: RootCubed's [NSMBW symbols](https://github.com/RootCubed/nsmbw-symbols), the NSMBW decompilation and the NSMBW address maps. The NSMBW function map was the starting point for this game's map.
- **[DriftDroid](https://github.com/dorPXP/driftdroid)** and **[Galaxy-Android](https://github.com/OpenPort-Labs/Galaxy-Android)**: other Wii-to-Android ports, read for reference.

Bundled third-party components and their licenses are listed in [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).

## License

GPLv3, see [`LICENSE`](LICENSE) and [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md). APKs are published only on this repository's [Releases](../../releases) page.

</div>

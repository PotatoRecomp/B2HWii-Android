# Sengoku Basara 2 Heroes for Android

<div align="justify">

An unofficial port of *Sengoku Basara 2 Heroes* (Wii, NTSC-J, `RBSJ08`) to Android (arm64) and Windows.

The game's PowerPC code is recompiled to C++ ahead of time on a PC and built into a native app, together with a runtime that stands in for the Wii's system software and a GX renderer on Vulkan. Nothing is emulated on the phone.

Port made by Xu, continuing the work of WitherBlitz with the help of WiiCompiled's code, and entirely coded by Claude Opus 5.5. Based on [NSMB-Wii-Multiplayer-Recompilation](https://github.com/WitherBlitz/NSMB-Wii-Multiplayer-Recompilation) and [WiiCompiled](https://github.com/patchzyy/Wiicompiled), both GPLv3 (see [Credits](#credits)).

</div>

> [!NOTE]
> This repository contains no game assets and no game code. You need a dump of your own legally acquired disc.
>
> This is an unofficial fan project, not affiliated with or endorsed by Capcom. Sengoku Basara, its characters and its logo belong to Capcom.

<div align="justify">

**Contents:** [Status](#status) · [Features](#features) · [Getting started](#getting-started) · [Saves](#saves) · [Reporting a problem](#reporting-a-problem) · [Building](#building) · [Credits](#credits) · [License](#license)

## Status

**1.0** (November 2026), the first release. The whole game is playable on Android and Windows, from boot through Story battles, with sound, saving and loading.

| Class | Phone | Chip / GPU | Battles |
|---|---|---|---|
| Flagship | RedMagic 8S Pro | Snapdragon 8 Gen 2 / Adreno 740 | 60 fps |
| Upper mid | Poco X7 Pro | Dimensity 8400 / Mali-G720 MC7 | 48–60 fps |
| Mid | Infinix GT30 | Dimensity 7300 / Mali-G615 | 48–60 fps |
| Mid-low | phone with Helio G99 | Helio G99 / Mali-G57 MC2 | about 30 fps in busy battles, with the Low-End Preset |
| Low | Redmi Note 12 Pro | Snapdragon 732G / Adreno 618 | 35–57 fps |
| Low | TECNO KN4n | Helio G81 / Mali-G52 MC2 | 37–60 fps |
| Low | realme RMX3710 | Helio G85-class / Mali-G52 MC2 | steady 30 fps with the 30 FPS Lock |
| Entry | itel A666LN | Unisoc T606 / Mali-G57 MP1 | 23–38 fps, with the Low-End Preset |
| Tablet | Redmi Pad SE | Snapdragon 680 / Adreno 610 | needs Turnip (Settings > Graphics > GPU Driver) |

The full change history is in the app (**What's New**) and on the [Releases](../../releases) page.

### New in 1.0

- Touch controls in two styles (Settings > Controls > Button Style): **Modern** shows a picture for what each button does, **Classic** shows the controller's letters. A green, B red, X blue, Y amber.
- A layout editor that pauses the game (Back > Layout): move every control on its own, A, B, X and Y too, and size it from 50% to 200% with the slider, the − and + buttons or a pinch. Controls snap to the centre lines and to each other.
- The right stick hides while Touch camera is on, and comes back when it is off.
- Low-End Preset, Display Resolution (960p, 720p, 480p), Sustained Performance and a Textures tab.
- Sound straight to Android's low-latency output: less crackle on slow phones.
- Fix: Dynamic Resolution with the 30 FPS Lock no longer lowers the picture while the game holds 30 fps.

## Features

| Area | What it has |
|---|---|
| **Performance** | Low-End Preset, Display Resolution, Frameskip, 30 FPS Lock, Dynamic Resolution, GX Thread, Thermal Control, Sustained Performance. A device profile picks the settings on first start. |
| **Graphics** | Render resolution 0.5×–4×, FSR1 and SGSR upscaling, Texture Detail (trilinear and anisotropic filtering), Vulkan renderer choice, custom GPU drivers (Turnip, malisx2). |
| **Controls** | Touch controls in Modern or Classic style with a layout editor (move and size every button), touch camera (with Invert Camera), tilt and shake, gamepads in the Classic Controller layout, and on Windows a keyboard with rebindable keys. |
| **Launcher** | Imports ISO or RVZ on the phone, Continue and Stop, in-game pause menu, performance overlay, cheats, save backup and import, Share Log, What's New, English and Indonesian. |
| **Textures** | Custom textures (.png or .dds, Dolphin names), texture dumping, texture key reports, HD Textures Live (2× upscale while playing). |

## Getting started

**Requirements:** Android 11 or newer, arm64, with Vulkan 1.1.

1. Install the APK from this repository's [Releases](../../releases) page. Do not use APKs from anywhere else.
2. Open the app and allow file access when asked.
3. Pick your own dump of the game (`.iso` or `.rvz`, Wii NTSC-J `RBSJ08`). The app imports it once.
4. Tap **Play**. The first start chooses settings for your phone; you can change them any time in the tabs.

The release (`Basara 2 Heroes`) is its own app, installed next to the Final Beta (`Basara Beta`). To bring your Beta save over: in Basara Beta, Settings > Save Data > **Back Up**; in Basara 2 Heroes, **Import** that backup; then uninstall Basara Beta.

## Saves

The save lives in the app's own storage. In **Settings > Save Data**:

- **Back Up** writes `basara2heroes-save-DATE.zip` to `Basara2Heroes/Saves` (`Basara2Heroes-Beta/Saves` in the Beta). Copy it to a PC or cloud drive to keep it safe.
- **Import** replaces the save on the phone. It accepts:
  - a `.zip` made by Back Up,
  - a bare `basara2w.dat`,
  - a `data.bin` copied to SD from a Wii (Data Management) or exported by Dolphin (Export Wii Save).
- **Undo Last Import** brings back the save from before the last import.

## Reporting a problem

In the app, Settings > System > **Share Log** puts the logs of the runs you pick (the crashed one first) and your device info in one zip. Send it, with what you were doing, to one of:

- a new [GitHub issue](../../issues),
- the Telegram channel: [t.me/indonesiarecompcomunity](https://t.me/indonesiarecompcomunity),
- the Discord server: [discord.gg/qcXXP7A2](https://discord.gg/qcXXP7A2).

The logs are also on the phone, one folder per game start: `Basara2Heroes/Logs/<date and time>/` (`Basara2Heroes-Beta/...` in the Beta).

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
..\third_party\gradle\bin\gradle.bat assembleRelease '-PbasaraVersionName=1.0' '-PbasaraVersionCode=19' '-PbasaraCheats=true'
# a Beta: -PbasaraBeta '-PbasaraBetaNumber=N'. In PowerShell quote each -P...=x.y, or "1.0" splits.
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
- **[game-icons.net](https://game-icons.net)** by Lorc, Delapouite and contributors (CC BY 3.0): the touch controls' Modern icons.
- **[DriftDroid](https://github.com/dorPXP/driftdroid)** and **[Galaxy-Android](https://github.com/OpenPort-Labs/Galaxy-Android)**: other Wii-to-Android ports, read for reference.

Bundled third-party components and their licenses are listed in [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).

## License

GPLv3, see [`LICENSE`](LICENSE) and [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md). APKs are published only on this repository's [Releases](../../releases) page.

</div>

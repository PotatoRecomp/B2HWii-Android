# Sengoku Basara 2 Heroes for Android

<div align="justify">

An unofficial port of *Sengoku Basara 2 Heroes* (Wii, NTSC-J, `RBSJ08`) to Android (arm64) and Windows.

The game's PowerPC code is recompiled to C++ ahead of time on a PC and built into a native app, together with a runtime that stands in for the Wii's system software and a GX renderer on Vulkan. Nothing is emulated on the phone.

Port made by xu@shiinobu, continuing the work of WitherBlitz with the help of WiiCompiled's code, and entirely coded by Claude Opus 5.5. Based on [NSMB-Wii-Multiplayer-Recompilation](https://github.com/WitherBlitz/NSMB-Wii-Multiplayer-Recompilation) and [WiiCompiled](https://github.com/patchzyy/Wiicompiled), both GPLv3 (see [Credits](#credits)).

</div>

> [!NOTE]
> This repository contains no game assets and no game code. You need a dump of your own legally acquired disc.
>
> This is an unofficial fan project, not affiliated with or endorsed by Capcom. Sengoku Basara, its characters and its logo belong to Capcom.

<div align="justify">

**Contents:** [Status](#status) · [Features](#features) · [Getting started](#getting-started) · [Saves](#saves) · [Reporting a problem](#reporting-a-problem) · [Building](#building) · [Credits](#credits) · [License](#license)

## Status

**Current release: [1.2](../../releases/latest)** (November 2026). The whole game is playable on Android and Windows, from boot through Story battles, with sound, saving and loading. What changed in each version is on the [latest release](../../releases/latest) page, all versions on [Releases](../../releases), and in the app under **What's New**.

### Tested phones

Results from our own phones and from testers' logs. "Version" is the build it was last tested on; a phone not retested may run better on the current release.

**Adreno and Mali**

| Phone | Chip / GPU | Android | Battles | Version |
|---|---|---|---|---|
| RedMagic 8S Pro | Snapdragon 8 Gen 2 / Adreno 740 | 14 | 60 fps | 1.1 |
| Poco X7 Pro | Dimensity 8400 / Mali-G720 MC7 | | 48–60 fps | 1.0 |
| Infinix GT30 | Dimensity 7300 / Mali-G615 | | 48–60 fps | 1.0 |
| phone with Helio G99 | Helio G99 / Mali-G57 MC2 | | about 30 fps in busy battles, with the Low-End Preset | 1.0 |
| Redmi Note 12 Pro | Snapdragon 732G / Adreno 618 | | 35–57 fps, about 51 in busy battles | 1.2 |
| OPPO phone with Snapdragon 680 | Snapdragon 680 / Adreno 610 | | 50–60 fps, 40–45 when busy | 1.2 test |
| Redmi Pad SE | Snapdragon 680 / Adreno 610 | | best with Turnip (Settings > Graphics > Install GPU Driver) | 1.0 |
| realme 5 | Snapdragon 665 / Adreno 610 | 10 | about 30 fps on Vulkan, stable through a 7-minute session with 7 pauses | 1.1 |
| Redmi Note 7 | Snapdragon 660 / Adreno 512 | 10 | about 22 fps (OpenGL ES, chosen automatically; 30 FPS Lock, 0.5x) | 1.1 |
| TECNO KN4n | Helio G81 / Mali-G52 MC2 | | 37–60 fps | 1.0 |
| realme RMX3710 | Helio G85-class / Mali-G52 MC2 | | steady 30 fps with the 30 FPS Lock | 1.1 |
| Redmi 12 | Helio G88 / Mali-G52 MC2 | | plays; drops in ultimates, where Dynamic Resolution steps in | 1.2 test |
| itel A666LN | Unisoc T606 / Mali-G57 MP1 | | 23–38 fps, with Ultra Low | 1.2 test |

**PowerVR** (many MediaTek and Unisoc phones): less stable and less tested. Use **Ultra Low**, and send **Share Log** when something goes wrong.

| Phone | Chip / GPU | Android | Result | Version |
|---|---|---|---|---|
| OPPO CPH2209 | Helio P90 / PowerVR GM9446 | 12 | about 30 fps; the black picture on Vulkan is fixed (1.2 sets it up by itself) | 1.2 test |
| Redmi phone with MT6855 | Dimensity (MT6855) / PowerVR BXM-8-256 | | plays; the battle crashes ("invalid wrap mode 3", "Unsupported component type") are fixed in 1.2 | 1.2 test |
| OPPO CPH2269 | Helio G35 / PowerVR GE8320 | | closed on start in 1.0; starts since 1.1 | 1.1 |
| Samsung SM-A045F | Helio G35-class / PowerVR GE8320 | 14 | runs, but only about 11 fps: its CPU is below what the game needs | 1.1 |
| Infinix X680B | Helio (MT6762) / PowerVR GE8320 | 10 | black screen with sound in 1.1; not retested with 1.2's PowerVR fixes | 1.1 |
| Infinix X669C | Helio (MT6765) / PowerVR GE8320 | 12 | black screen with sound in 1.1; not retested with 1.2's PowerVR fixes | 1.1 |

Your phone is not here, or runs differently? Send a Share Log (see [Reporting a problem](#reporting-a-problem)), working or not: these reports are what fills this list.

## Features

| Area | What it has |
|---|---|
| **In-game menu** | No launcher: the app opens straight into the game. Every setting is in the game's menu (Back, or the gear): Display, Graphics, Performance, Controls, Save Data, Cheats, Textures and System, with Restart for the settings that need it. English and Indonesian. |
| **Performance** | Low-End and Ultra Low presets, Display Resolution (960p to 360p), Frameskip, 30 FPS Lock, Dynamic Resolution, GX Thread, Thermal Control, Sustained Performance, Disable Fog, Reduce Effects (Experimental). A device profile picks the settings on first start. |
| **Graphics** | Renderer choice (Auto, Vulkan, WebGPU 1.2, WebGPU 1.0, OpenGL ES), render resolution 0.5×–4×, FSR1 and SGSR upscaling, Texture Detail (trilinear and anisotropic filtering), Real Widescreen, Camera Distance, custom GPU drivers (Turnip, malisx2). |
| **Controls** | Touch controls in Modern or Classic style with a layout editor (move and size every button), touch camera (with Invert Camera), tilt and shake, gamepads in the Classic Controller layout with remappable buttons (generic USB pads too), and on Windows a keyboard with rebindable keys. |
| **Textures** | Texture mods (.png or .dds, Dolphin names) in their own folders, ticked, ordered and checked in the Textures tab, with `Textures/Loaded.txt` listing what was replaced. Texture dumping, texture key reports, HD Textures Live (2× upscale while playing). |
| **Tools** | Performance overlay (Bar or Minimal style), cheats, save backup and import, Share Log, What's New, in-app updates. |

## Getting started

**Requirements:** Android 10 or newer, arm64. Vulkan is recommended; OpenGL ES is available as an experimental renderer.

1. Install the APK from this repository's [latest release](../../releases/latest). Do not use APKs from anywhere else.
2. Open the app and allow file access when asked.
3. Pick your own dump of the game (`.iso` or `.rvz`, Wii NTSC-J `RBSJ08`). The app imports it once; after that the icon goes straight into the game.
4. The first start chooses settings for your phone. Change them any time from the game's menu (Back, or the gear).

The app checks this repository for new versions and offers them in the app. Installing a new version over the old one keeps your save and settings.

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
tools\b-android.ps1 -Mode build                                         # libmain.so (aurora/)
tools\b-android.ps1 -Mode stage
tools\b-android.ps1 -Mode build -BuildDir generated\android-build10     # libmain10.so (aurora10/, WebGPU 1.0)
tools\b-android.ps1 -Mode stage10 -BuildDir generated\android-build10
cd app
..\third_party\gradle\bin\gradle.bat assembleRelease "-PbasaraVersionName=1.2" "-PbasaraVersionCode=45" --no-daemon -q
# a Beta: -PbasaraBeta "-PbasaraBetaNumber=N". In PowerShell quote each -P...=x.y, or "1.2" splits.
```

Optional: `tools\b-pgo.ps1` for a profile-guided (PGO + ThinLTO) Android build.

| Path | Contents |
|---|---|
| `app/` | Android app (Gradle module): in-game menu, settings, touch controls, updater |
| `runtime/` | Host runtime and Wii SDK HLE (OS, GX, AX, NAND/DVD, VI), performance overlay |
| `aurora/` | GX renderer on WebGPU (Dawn), native Vulkan and OpenGL ES |
| `aurora10/` | The 1.0 renderer (aurora from v1.0 with crash fixes), built as `libmain10.so` for Renderer > WebGPU 1.0 |
| `translator/` | PowerPC-to-C++ translator (C#/.NET 8) |
| `config/basara/` | Game configuration: `recomp.yml`, `MAP.txt` |
| `tools/` | Build, run and analysis scripts |

How the pieces fit together: [docs/architecture.md](docs/architecture.md).

## Credits

This project is a modified version of NSMB-Wii-Multiplayer-Recompilation, which is built on WiiCompiled. It was changed from October 2026 to run *Sengoku Basara 2 Heroes*.

- **[NSMB-Wii-Multiplayer-Recompilation](https://github.com/WitherBlitz/NSMB-Wii-Multiplayer-Recompilation)** by WitherBlitz: the Android app and the runtime this port started from.
- **[WiiCompiled](https://github.com/patchzyy/Wiicompiled)** by patchzyy and contributors: the static recompiler and runtime underneath it.
- **datfooldive**: the touch button design and the Unlock All idea.
- **[Jude (@YlPorts)](https://github.com/YlPorts)**: the Android draw-count and Mali GPU changes.
- **[KartPad](https://github.com/chrissotraidis/kartpad)**: WiiCompiled on Android, whose approach and game-file import steps this port follows.
- **[aurora](https://github.com/encounter/aurora)** by Luke Street: the GX rendering and windowing backend.
- **[Dawn](https://dawn.googlesource.com/dawn)**: Google's WebGPU implementation, used by aurora for Vulkan and Direct3D.
- **[Dolphin Emulator](https://github.com/dolphin-emu/dolphin)**: the reference for Wii hardware behavior and renderer techniques, the free DSP coefficient ROM and the default WiiConnect24 tree in the runtime, the RVZ format the app reads, and the Wii save (`data.bin`) format.
- **The PCSX2 widescreen patch** for the PS2 version (`SLPM-66848`): research for Real Widescreen.
- **The NSMBW reverse-engineering community**: RootCubed's [NSMBW symbols](https://github.com/RootCubed/nsmbw-symbols), the NSMBW decompilation and the NSMBW address maps. The NSMBW function map was the starting point for this game's map.
- **[game-icons.net](https://game-icons.net)** by Lorc, Delapouite and contributors (CC BY 3.0): the touch controls' Modern icons.
- **[Roboto](https://fonts.google.com/specimen/Roboto)** by Google (Apache 2.0): the performance overlay's font.
- **[DriftDroid](https://github.com/dorPXP/driftdroid)** and **[Galaxy-Android](https://github.com/OpenPort-Labs/Galaxy-Android)**: other Wii-to-Android ports, read for reference.

Bundled third-party components and their licenses are listed in [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).

## License

GPLv3, see [`LICENSE`](LICENSE) and [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md). APKs are published only on this repository's [Releases](../../releases) page.

</div>

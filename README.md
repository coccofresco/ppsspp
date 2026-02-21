PPSSPP VR for Windows (PC VR Fork)
====================================

> **A fork of [PPSSPP](https://github.com/hrydgard/ppsspp) that brings PSP VR gameplay to Windows PC via OpenXR.**
> Based on the OpenXR VR implementation for Quest and Pico by [Lubos Vonasek (lvonasek)](https://github.com/lvonasek).

**Status: Alpha** — core features work, expect rough edges.

Download the latest build from [Releases](https://github.com/coccofresco/ppsspp/releases).

What this is
------------

Play PSP games in VR on any OpenXR-compatible PC headset. This fork adapts the existing Quest/Pico VR codebase (originally developed by lvonasek and merged into upstream PPSSPP) to work natively on Windows with OpenXR runtimes like Quest Link, SteamVR, and Virtual Desktop.

Features (Alpha)
----------------

- **Virtual cinema screen** — PSP games on a floating screen in VR
- **Stereoscopic 3D** — true per-eye depth rendering for supported games
- **Head-tracked camera** — look around the game world (compatible games)
- **Anti-flickering** — smooth viewing regardless of PSP framerate
- **Stereo intensity slider** — adjust 3D depth (0-150%)
- **Quick toggle** — button combo to switch stereo on/off

Requirements
------------

- Windows 10/11
- OpenXR-compatible VR headset
- OpenXR runtime installed (SteamVR, Oculus, or WMR)
- Gamepad (Xbox controller recommended)

Installation
------------

1. Download the latest `.zip` from [Releases](https://github.com/coccofresco/ppsspp/releases)
2. Extract to any folder
3. Connect your VR headset
4. Launch `PPSSPPWindows.exe`
5. Load a PSP game — it appears on a virtual screen in VR
6. VR settings are in **Settings > VR**

Known limitations
-----------------

- OpenGL backend only (Vulkan VR not yet supported)
- Gamepad required (no VR controller mapping yet)
- Per-game VR profiles not yet implemented
- SteamVR uses quad composition layer (no cylinder support)
- Quest Link wireless can be intermittent — USB cable recommended

Tested runtimes
---------------

| Runtime | Status |
|---------|--------|
| Quest Link (USB) | Working |
| SteamVR | Working (quad fallback) |
| Virtual Desktop | Untested |

Building from source
--------------------

```bash
git clone --recursive https://github.com/coccofresco/ppsspp.git
cd ppsspp
cmake -B build-vr -DOPENXR=ON -DUSING_QT_UI=OFF
cmake --build build-vr --target PPSSPPWindows --config Release
```

The built executable will be in `build-vr/Release/`. Copy the `assets/` folder next to the exe to run.

Credits
-------

- **[PPSSPP](https://github.com/hrydgard/ppsspp)** — Henrik Rydgard and contributors
- **[Lubos Vonasek (lvonasek)](https://github.com/lvonasek)** — original OpenXR VR implementation for Quest and Pico, merged into upstream PPSSPP
- **Windows OpenXR port** — coccofresco

License
-------

GPL 2.0+, same as upstream PPSSPP.

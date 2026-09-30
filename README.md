# DyingLight2VR

An experimental Windows x64 OpenXR VR mod for **Dying Light 2 Stay Human**, using the game's DirectX 11 renderer.

- Both eyes render the same simulation frame. No alternate-eye rendering (AER).
- Headset tracking, headset field of view and recommended eye resolution.
- Automatic VR startup and recovery across loading and inventory/crafting/skills/map menus.
- Stereo HUD, menus and video tooltips; corrections for culling, flashlight shadows, AO and water/reflections.
- DLSS remains available. Mouse/keyboard and ordinary gamepads are supported; motion controls are not implemented.

## Download and install

Download **DyingLight2VR-0.1.0.zip** from [Releases](https://github.com/farmerarmor/DyingLight2VR/releases), then extract it into a permanent folder. The GitHub source ZIP does not contain the compiled mod.

1. In the game, select **DirectX 11**, then close the game.
2. Run **Install-VR.cmd**. It detects the Steam install or asks for the game folder. Only the exact binaries listed in `baseline.json` are supported; an update may require a new mod build.
3. Activate your headset's OpenXR runtime. Quest Link with the Oculus OpenXR runtime was used for headset testing.
4. Run **Launch-VR.cmd**. It applies the headset's recommended output resolution and launches the Steam game. DLSS/upscaling and other graphics preferences are preserved. Original video settings are backed up under `diagnostics`.

Requires Windows x64 and the Microsoft Visual C++ 2015-2022 x64 runtime. Steam app ID: 534380. This is a community mod, not an official Techland release.

## Settings

`DL2VR.ini` is installed beside `winmm.dll` in `ph/work/bin/x64` under the game folder. Existing settings are preserved. Restart the game after editing.

```ini
[Controls]
LockVerticalLook=1

[Rendering]
DisableTemporalFilter=0
```

`LockVerticalLook=1` blocks mouse and controller vertical camera input while VR is active. Horizontal turning, headset pitch and unrelated input actions remain available. Set it to `0` to restore normal vertical input.

`DisableTemporalFilter=1` disables the identified native temporal filter. F11 toggles and saves this setting. It does not disable DLSS.

## Controls

| Key | Function |
| --- | --- |
| F6 | Recenter |
| F7 | Toggle VR |
| F9 | Toggle shared flashlight-shadow correction (ON at launch) |
| F11 | Toggle native temporal filter suppression |
| F8 | Save diagnostic captures; allow ten seconds for saving |
| End | Enable shader hunting |
| Page Up / Page Down | Select a shader to disable while hunting |
| Delete | Mark the selected shader |
| Home | Reset and disable shader hunting |

Diagnostic comparisons: F1 toggles UI correction, F2 toggles the culling bypass, F3 toggles intermediate-present suppression, F4 switches the optimized two-render path/fallback, F5 toggles the auxiliary camera correction, F10 hides the GUI, and F12 cycles temporal-history modes. These are development controls; leave their startup defaults for ordinary play. Home does not reset those independent toggles.

## Status and limitations

The stereo renderer, menu transitions, UI and flashlight corrections were tested in the headset. The default-on vertical-input option was also confirmed in-game with horizontal turning, headset tracking and menu input preserved. Residual weapon-edge ghosting against water and some ground-detail artifacts may remain. Other headsets/runtimes and other game binary versions have not been validated. This is an experimental release.

## Remove or update

Close the game. Run `Uninstall-VR.ps1 -GameRoot "your game folder"` to remove the installed mod DLL after checking its installation receipt. Your INI and game settings remain. To update, extract the new release and run its installer; it verifies the existing managed DLL and preserves settings.

## Build

Use Visual Studio 2022 with Desktop development with C++, the Windows SDK, and CMake:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

`bin/winmm.dll` is the mod. Copy `build/Release/XrRuntimeChecks.exe` into `bin` for the launcher. The bundled OpenXR loader library is linked statically. MinHook and OpenXR licenses are retained under `vendor`; checksum attribution is in `THIRD_PARTY_NOTICES.md`.

Tests are standalone executables in the build directory. `VerticalLookChecks`, `CameraMathChecks`, `TrackedCameraChecks`, `UiProjectionChecks`, `TemporalHistoryChecks`, `ShaderHunterChecks`, `TextureHistoryChecks`, `EffectsCaptureChecks`, and `DlssCaptureChecks` cover input, math and GPU state. Shader-patch checks additionally require locally captured original game shaders; game shader bytecode and captures are not distributed here. Build/tests cannot replace a headset test.

# DyingLight2VR

An experimental Windows x64 OpenXR VR mod for **Dying Light 2 Stay Human**. Version **0.2.0** uses the game's **DirectX 12** renderer.

- Native same-frame stereo: both eyes render the same simulation frame, with no alternate-eye rendering (AER).
- Headset tracking, headset field of view and recommended eye resolution; DLSS remains available.
- Automatic VR startup and recovery across loading and menu transitions.
- Inventory, crafting, skills, map and the other shared player submenus appear on a fixed panel in front of you, so you can look around them. The panel stays anchored when switching tabs.
- Gameplay quest/POI markers respond to head movement. The crosshair follows native mouse/controller weapon aim instead of the headset.
- Stereo UI, corrected headset colors, water/reflection histories, culling and flashlight-shadow corrections.
- Asynchronous GPU copies and allocator reuse retain the tested performance improvements.

Mouse/keyboard and ordinary gamepads are supported. Motion controls are not implemented.

## Download and install

Download **DyingLight2VR-0.2.0-DX12.zip** from [Releases](https://github.com/farmerarmor/DyingLight2VR/releases/tag/v0.2.0), then extract it into a permanent folder. The automatically generated source ZIP does not contain the compiled mod.

1. Select **DirectX 12** in the game, then close it. The release does not change your renderer selection.
2. Run **Install-VR.cmd**. It detects the Steam install or asks for the game folder. It checks the exact supported binaries in `baseline.json` before installing.
3. Activate your headset's OpenXR runtime. Quest Link/Oculus OpenXR was used for headset testing.
4. Run **Launch-VR.cmd**. It sets the output resolution to the headset recommendation, preserves DLSS/upscaling and other graphics preferences, and launches the Steam game. It saves a video-settings backup under `diagnostics`.

Requires Windows x64 and the Microsoft Visual C++ 2015-2022 x64 runtime. Steam app ID: 534380. A game update may require a new mod build. This is a community mod, not an official Techland release.

The installer can replace an earlier managed release and preserves your INI. If it finds an unrelated `winmm.dll` or a DLL that does not match its installation receipt, it stops without overwriting it. Remove that mod through its own installer first.

## Controls

| Key | Function and startup state |
| --- | --- |
| F6 | Recenter headset tracking |
| F7 | Toggle VR; starts ON and automatically resumes through transitions |
| F9 | Toggle shared flashlight-shadow correction; starts OFF |
| Insert | Toggle water/reflection texture histories; starts ON |
| F1 | Toggle UI, gameplay-marker and weapon-aim crosshair correction; starts ON |
| F2 | Toggle culling correction; starts ON |
| F5 | Toggle auxiliary camera correction; starts OFF |
| F10 | Hide/show GUI |
| F12 | Cycle temporal histories: full correction (default) → off → DLSS only → full correction |
| F8 | Save shader inventory and a bounded flashlight diagnostic to the mod log |

For flashlight shadows, enable **F9**. Other comparison keys can reintroduce known visual problems. The performance improvements remain enabled independently of Insert.

DX11-only controls such as F3/F4/F11 and End/Page Up/Page Down/Delete/Home shader hunting are not active in this DX12 build. The legacy `LockVerticalLook` and `DisableTemporalFilter` INI settings are not read by this DX12 build; an existing INI is preserved for switching back to DX11.

## Status and limitations

The shipped DLL was confirmed in-headset, including the fixed submenu panels and weapon-aim crosshair. The crosshair indicates the native aiming direction; it is not a new collision-distance or ballistic-impact predictor. Gameplay remains stereo; the shared player menus intentionally use a flat, anchored panel.

Some lamp shadows, tooltip blur/progress indicators, ground-detail artifacts and temporal effects remain imperfect. Other headsets/runtimes have not been validated. OpenXR Toolkit caused startup freezes and menu-transition crashes during testing; leave it disabled. The native-AA work from later experimental branches is not included in this tested performance baseline.

## Update, remove or return to DX11

Close the game before updating or removing the mod. Run the new release's installer to update a managed installation. Run **Uninstall-VR.cmd** to remove the installed DLL after verifying its receipt; your INI and game settings remain.

The earlier **[0.1.0 DX11 release](https://github.com/farmerarmor/DyingLight2VR/releases/tag/v0.1.0)** remains available. To return to it, uninstall 0.2.0, select DirectX 11, close the game and install the old release. [DX11 documentation](README-DX11.md) describes that version only; use its release package, not the current DX12 installer.

## Build

Use Visual Studio 2022 with Desktop development with C++, the Windows SDK and CMake:

```powershell
cmake -S dx12 -B build-dx12 -A x64
cmake --build build-dx12 --config Release
```

The mod is `dx12/bin/winmm.dll`. The launcher helper is `build-dx12/Release/XrRuntimeChecks.exe`; place both in the release package's `bin` directory. The helper queries the runtime's eye resolution using the existing D3D11 OpenXR extension; the mod itself renders and submits through D3D12.

`dx12/src` is the exact source of the headset-tested DX12 build. The repository-root CMake project, `src` and `tests` retain the earlier DX11 implementation. Shared dependencies are in `vendor`, with their licenses. No game binaries, captured shaders, logs or private captures are included.

DX12 checks are standalone executables in the build directory. Crosshair, marker, map-panel, tracking, UI and temporal checks cover coordinate transforms and state handling. GPU checks exercise resource copying and shader patching; some require a runtime/headset or locally captured original shaders. Tests do not replace headset validation.

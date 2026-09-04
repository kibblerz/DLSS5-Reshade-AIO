# DLSS5 ReShade AIO

Bring Neural Rendering, DLAA/DLSS Super Resolution, and Frame Generation to supported 64-bit Windows games even when the game does not include those features. This is experimental software and currently supports D3D9, D3D11, D3D12, and Vulkan through ReShade.

> [!IMPORTANT]
> **Version 2.0 uses a new presentation and window-compatibility system.** It fixes input and reduced-window behavior in many games, but game compatibility can differ from the 1.x series. If a game has problems in 2.0 that it did not have before, install the [latest 1.x release (v1.7.24)](https://github.com/kibblerz/DLSS5-Reshade-AIO/releases/tag/v1.7.24). Do not mix the 1.x and 2.0 addon binaries.

> [!TIP]
> **Windowed mode is recommended for setup and normal use.** It reliably exposes a lower-resolution backbuffer for DLSS SR, and version 2.0.1 can place the processed output in a small preview beside the real ReShade window while you change settings. Closing ReShade restores the native-size processed output.

## Quick install

> [!IMPORTANT]
> Install both required binaries: `standalone-dlssnr.addon64` **and** `nvngx.dll`. The addon will not initialize with only the `.addon64` file. Releases starting with v1.7.16 also include the companion `DLSS5_AIO_Feed.fx` shader.

1. Install a 64-bit ReShade build with addon support into the folder containing the game's real executable. Launchers often use a different folder, so target the executable that renders the game.
2. Open the [latest release](https://github.com/kibblerz/DLSS5-Reshade-AIO/releases/latest) and download:
   - `standalone-dlssnr.addon64`
   - `nvngx.dll` (the required caller bridge)
   - `DLSS5_AIO_Feed.fx` (the companion guide shader)
3. Put both binary files beside the game's ReShade DLL and executable. RHI users may instead put them in `%LOCALAPPDATA%\RHI\Custom\Addons`.
4. Put `DLSS5_AIO_Feed.fx` in the game's ReShade shader directory, normally `reshade-shaders\Shaders`. It is uniquely named for this addon and will not replace the upstream DLSS5-Feeder project's `DLSS5_Feed.fx`.
5. Supply `nvngx_dlssnr.dll`, `nvngx_dlss.dll`, and optionally `nvngx_dlssg.dll` from sources whose licenses permit your use. These NVIDIA runtimes cannot be distributed in this repository. The simplest arrangement is to place them beside the addon; see [`runtime/README.md`](runtime/README.md).
6. Start the game and open ReShade. Confirm that **Standalone DLSS-NR + SR** appears under the Add-ons tab.

## First-launch setup

1. Disable the game's built-in **DLSS/upscaling, Frame Generation, and antialiasing**. The addon supplies its own pipeline.
2. Select **windowed mode** when available. It is the recommended mode because it usually creates a genuinely lower-resolution backbuffer and leaves room for the processed preview while ReShade is open. Fullscreen and borderless remain supported when a game handles them correctly.
3. Select the game resolution:
   - **Same as the monitor:** the addon automatically uses **DLAA** at a 1:1 render scale.
   - **Lower than the monitor:** the addon uses **DLSS Super Resolution** to reconstruct the image to the monitor's native size.
4. If a lower game resolution still reports **DLAA**, the game is still presenting a native-size backbuffer. Try windowed mode first, then borderless or fullscreen; restart after changing modes if necessary. Use whichever mode makes the overlay report **DLSS SR**.
5. Neural Rendering and Frame Generation are enabled by default and can be toggled independently in ReShade. Disabling both leaves an SR/DLAA-only pipeline.

Reduced-resolution DLSS SR can provide major performance improvements. Native-resolution DLAA instead prioritizes image quality.

- Press **F10** to compare the processed image with the original game output.
- Press **Ctrl+Alt+P** to cycle through the modern DLSS render presets **J → K → L → M**. NVIDIA Default remains selectable in ReShade. A three-second corner notice shows the selected preset and the mode it was designed to target; the ReShade status separately reports the pipeline's actual active DLAA/DLSS mode.
- Press **Ctrl+Alt+N** to cycle through Neural Rendering models **1 → 2 → 3**. A three-second corner notice confirms the selected model.
- While ReShade is open in a reduced window, version 2.0.1 moves the processed/F10 output into a small mouse-transparent preview beside it. This lets the real ReShade window receive normal clicks. Close ReShade to restore the fullscreen compositor.
- The addon draws its own FPS counter because third-party overlays may not appear through its presentation proxy.
- Logs are written to `%LOCALAPPDATA%\RHI\Logs\standalone-dlssnr.log`.

### DLSS render presets

These presets tune reconstruction behavior; they do not change the input resolution selected in the game.

- **L (recommended default):** The tested default for both 1080p and 1440p reconstruction; produced the least visible smearing in current game tests.
- **Default (NVIDIA):** Lets the NVIDIA runtime select a preset for the active DLAA/DLSS mode.
- **J:** May reduce some ghosting versus K, but can introduce more flickering.
- **K:** High-quality option for DLAA, Quality, and Balanced, but showed more smearing than L in current tests.
- **M:** Performance-oriented modern preset with much of L's image-quality behavior at speed closer to J/K.

### Experimental VORT motion integration

VORT motion integration is **disabled by default** because its optical-flow and guide-conversion passes can have a substantial performance cost. The addon normally uses its zero-motion fallback and does not require VORT.

To experiment with motion guidance, install VORT Motion and `DLSS5_AIO_Feed.fx` in the same ReShade shader search path, then enable **Enable VORT motion integration (experimental)** under the addon's Neural Rendering controls. The addon schedules both effects itself; leave their ordinary ReShade technique checkboxes disabled. Turn the option back off if performance drops or image quality does not improve. The option now supports both native D3D12 and the addon's D3D11-to-D3D12 transport.

## Troubleshooting — start here

Open **ReShade > Add-ons > Standalone DLSS-NR + SR**, then expand **Compatibility / troubleshooting**. Leave the automatic option enabled and change only the setting that matches your problem. Restart the game after changing any option marked as requiring a restart.

| What you see | Setting or action to use |
| --- | --- |
| **The image is small, stuck in a corner, or only occupies part of the screen** | Enable **Force reduced-window virtualization**. This is the first option to try for a wrongly sized image. Restart if the image does not settle immediately. |
| **The picture is correct, but mouse clicks land in the wrong place or only part of the screen is clickable** | Enable **Scale window input coordinates to render resolution**. It automatically enables **Force reduced-window virtualization**, which it requires. |
| **The game does not capture the mouse, the pointer escapes, or camera rotation stops at a screen edge** | Enable **Hide detached Windows cursor**. Turn it back off if the game needs the normal Windows cursor for its menus; automatic cursor handling works in most games. |
| **The addon initializes, but the processed picture stays in the original window, is missing, or is black** | Try **Detached native output (Vulkan compatibility)** and restart. This is most often needed by Vulkan or unusual windowed games. |
| **The original and processed pictures appear together, or the output looks transparent** | Enable **Opaque attached composition** and restart. |
| **The game crashes, freezes, or goes black when processed output begins or after a resolution change** | Enable **Serialized presentation (crash workaround)** and restart. If the game crashes before you can reach ReShade, hold **F8 while launching** to start in serialized safe mode. |
| **The addon stays on “waiting for Present” during D3D12 startup** | Try **Early proxy initialization (D3D11On12 compatibility)** and restart. Leave this disabled in games that already launch normally. |
| **A lower game resolution still reports DLAA instead of DLSS SR** | Switch to windowed mode first. DLSS SR activates only when the game creates a genuinely lower-resolution backbuffer. If needed, test borderless/fullscreen and restart after changing mode. |
| **The image becomes smeared, stretched, or wrongly sized after changing resolution** | Restart the game and set the desired resolution/display mode before loading gameplay. Live resolution changes remain game-dependent. |
| **The processed preview does not appear while ReShade is open** | Use windowed mode at a resolution below the monitor's native resolution. Fullscreen and borderless windows may leave no separate desktop area for the preview. Closing ReShade should still restore the native-size processed output. |
| **The addon is missing from ReShade** | Confirm the game is 64-bit, ReShade was installed with addon support, and `standalone-dlssnr.addon64` is beside the real game executable and ReShade DLL. |
| **The log says `required private runtime dependency missing`** | Install `nvngx.dll` beside the addon. Also supply `nvngx_dlssnr.dll` and `nvngx_dlss.dll`; `nvngx_dlssg.dll` is required for Frame Generation. |
| **The overlay reports fallback or zero-motion guides** | This is the normal default. VORT motion integration is optional and disabled by default because it may significantly reduce performance. To test it, install `DLSS5_AIO_Feed.fx` and VORT Motion under the configured ReShade shader path, then enable **Enable VORT motion integration (experimental)**. |
| **Vulkan waits for a shared frame** | Confirm ReShade's Vulkan layer is active. If no other ReShade effect is loaded, install `StandaloneBoundary.fx` so the required effects boundary runs. |
| **A game worked in 1.x but not in 2.0** | Remove the 2.0 `standalone-dlssnr.addon64` and use [v1.7.24, the latest 1.x release](https://github.com/kibblerz/DLSS5-Reshade-AIO/releases/tag/v1.7.24). Please include the game, API, display mode, and `standalone-dlssnr.log` when reporting the 2.0 regression. |

### Early proxy initialization

This is a compatibility workaround for certain **D3D12 games that use D3D11On12**. It creates the addon's native-size output window before the game's first Present call, avoiding a startup deadlock seen in some titles. It does not improve image quality or performance and is not intended for D3D9, ordinary D3D11, or Vulkan games.

1. Leave the option **off** unless a D3D12 game hangs, freezes, or never gets past waiting for Present/native proxy initialization.
2. In ReShade, open **Add-ons > Standalone DLSS-NR + Super Resolution**, expand **Compatibility / troubleshooting**, and enable **Early proxy initialization (D3D11On12 compatibility)**.
3. Completely close the game and start it again. The setting only takes effect during the next process launch; do not switch it on or off while testing the same game session.
4. If the game now starts, leave the option enabled for that game only. ReShade saves addon settings per installation.
5. If it causes a black screen, crash, or a new startup problem, turn it back off and restart. If the menu is inaccessible, close the game and set `EarlyProxyInitialization=0` under `[Standalone.DLSSNR]` in the game's `ReShade.ini`.

The persistent log records `early_proxy=enabled` at startup when the saved setting was applied. A queue mismatch is rejected rather than used; the log will say that the early proxy was quarantined and the normal authoritative queue was adopted.

## Known limitations

- Occasional stuttering or uneven Frame Generation pacing may occur.
- Changing resolution while running may cause visual glitches; restarting usually fixes them.
- The processed side preview is designed primarily for reduced-resolution windowed mode. Fullscreen and borderless behavior while ReShade is open remains game-dependent.
- No Man's Sky and potentially other Vulkan games may not display the ReShade menu correctly.
- Additional game-specific and Vulkan issues are expected.
- The experimental VORT NR rejection mask currently behaves more like a hard gate than a gradual blend at nonzero strength. Leave it disabled unless testing this feature; strength zero is an exact bypass that restores NVIDIA automatic masking.

## What changed in 2.0

Version 2.0 replaces the older always-detached presentation behavior with a compatibility-aware compositor. It attaches the finished native-resolution image directly to the game window when that is safe and automatically uses a detached native-size output for genuinely reduced windows and Vulkan cases. The game window is left untouched by default.

The new **Compatibility / troubleshooting** panel provides opt-in fixes for games with unusual window or input behavior: reduced-window virtualization, logical client virtualization, scaled input coordinates, detached output and cursor handling, opaque composition, serialized presentation, and early D3D11On12 proxy initialization. Automatic presentation remains the recommended default.

Resolution transitions are serialized outside the game's DXGI callback, failed sessions can recover into serialized mode by holding **F8** during launch, and startup contract changes hold the last completed native frame instead of repeatedly exposing the low-resolution game surface.

Because presentation behavior varies substantially between engines, 2.0 may work better or worse than 1.x in a particular game. Keep [v1.7.24](https://github.com/kibblerz/DLSS5-Reshade-AIO/releases/tag/v1.7.24) available as the stable 1.x fallback and report regressions with the game name, graphics API, display mode, and persistent addon log.

### Version 2.0.2

The DLSS Super Resolution stage now exposes NVIDIA's modern render presets **J, K, L, and M**, while legacy and deprecated presets remain hidden. **Ctrl+Alt+P** cycles the DLSS presets and **Ctrl+Alt+N** cycles Neural Rendering models 1–3 without opening ReShade. Three-second native-output notices confirm each selection; DLSS notices also identify the preset's intended role. The active DLAA/DLSS quality contract is still selected automatically from the game's input resolution and is reported separately in ReShade.

### Version 2.0.3

VORT motion guidance is now an explicit experimental option and defaults to disabled, avoiding its potentially large per-frame performance cost unless the user chooses to test it. The guide bridge now supports D3D11 games in addition to native D3D12. DLSS Preset L is the new recommended default because current 1080p and 1440p game testing showed substantially less smearing than the other modern presets. This release does not include the later San Andreas submission-ring or window-resize experiments.

### Version 2.0.1

Opening the primary ReShade menu with detached presentation now moves the processed output into a separate, aspect-correct side preview. The preview does not accept mouse input, so clicks go to the game's real ReShade window without synthetic forwarding or a duplicate interactive cursor. F10 continues to choose processed or raw-stretched output in the preview, and closing ReShade restores the fullscreen native-size compositor. Windowed mode is recommended because it provides predictable space for both windows.

## Companion guide shader

`DLSS5_AIO_Feed.fx` is this project's ReShade companion effect. The addon schedules it at Present after VORT Motion and before NGX evaluation. It converts VORT's optical flow to the pixel-space motion format expected by DLSS, captures ReShade depth, and creates a history-rejection mask around invalid reprojections and depth boundaries.

Install it under the game's configured ReShade shader search path, normally `reshade-shaders\Shaders`. Install the third-party VORT Motion shader alongside it if you want same-frame optical-flow guidance. When either integration is unavailable, the addon falls back to internal zero-motion and constant-depth guides.

The filename, technique, and exported guide resources use the `DLSS5_AIO_*` namespace. This is intentionally separate from the original DLSS5-Feeder project's `DLSS5_Feed.fx`, so both shaders can coexist without one overwriting or binding the other.

## Technical details

This project contains two independently useful pieces:

- `lab/`: a deterministic D3D12 test program that reverse-engineers and validates the private DLSS-NR feature-18 contract without launching a game.
- `addon/`: a standalone ReShade addon for D3D9, D3D11, D3D12, and 64-bit Windows Vulkan games. It does not hook or depend on the ShortFuse addon.

## Proven pipeline

The laboratory established that this feature-18 package is a neural-rendering stage, not a spatial upscaler by itself. NVIDIA's own `DLSSNRComputeScalingRatioCallback` resolves every supported NR quality preset to `1.0`. In a direct 2x probe, feature 18 evaluates successfully but writes exactly the input-sized top-left quadrant (25% of the target). The working and efficient layout therefore gives NR render-sized color/output allocations, then gives only the downstream DLSS Super Resolution feature a native-sized output:

`low-resolution game color + depth + motion -> DLSS-NR feature 18 -> DLSS Super Resolution -> native presentation`

At native render resolution, the downstream reconstruction stage instead uses NVIDIA's explicit 1:1 DLAA mode:

`native-resolution game color + depth + motion -> DLSS-NR feature 18 -> DLAA -> native presentation`

The matrix validates all three input profiles and all three NR models using this compact layout. It verifies full output coverage and verifies that changing NR intensity changes the final native-output checksum, which demonstrates that NR is materially participating rather than being bypassed. A separate expected-failure probe records the NR-only 25% coverage boundary so a later runtime change cannot be mistaken for the current contract.

## Run the laboratory

The complete automated validation can be run from PowerShell:

```powershell
cd streamline\nr-standalone\lab
.\validate.ps1
```

It builds the lab, runs all nine profile/model combinations, verifies native-frame coverage, then runs an intensity A/B probe and requires the final checksum to change. The resulting `validation-summary.json` explicitly reports `fullCoverage` and `nrMateriallyActive`.

The matrix can also be run directly from an x64 developer command environment, or a normal command prompt on this machine:

```bat
cd streamline\nr-standalone\lab
build-lab.bat
run-matrix.bat
```

The reverse-engineered runtime callback, stats callback, Style/preset/quality
matrix, and direct runtime-scaling results are documented in
[`lab/PRIVATE-CONTRACT-FINDINGS.md`](lab/PRIVATE-CONTRACT-FINDINGS.md). These
private-contract tests remain laboratory-only and are not enabled in the addon.

Expected summary:

```text
Matrix complete: 0 failing cases out of 9.
```

Each case writes a text log, a JSON result, and a PPM output. `nr-lab.log` contains the latest detailed run. A passing result requires successful feature-18 creation/evaluation, successful DLSS SR creation/evaluation, and over 95% changed-pixel coverage with every quadrant over 90%.

## Build from source

Clone with submodules, then install the NVIDIA NGX SDK headers/import library and Khronos Vulkan headers using the instructions under `external\DLSS5-Feeder\external\ngx` and `external\DLSS5-Feeder\external\vulkan`. The closed-source NVIDIA runtime DLLs are not stored in this public repository; place locally obtained copies in `runtime\` as described in `runtime\README.md`.

Run `addon\build.bat`. The available runtime set is emitted under `addon\build`:

- `standalone-dlssnr.addon64`
- `nvngx.dll` (the caller-identity bridge required by the NR snippet)
- `nvngx_dlssnr.dll` (the exact tested NR runtime)
- `nvngx_dlss.dll`
- `nvngx_dlssg.dll`
- `DLSS5_AIO_Feed.fx`

Every GitHub release from v1.7.16 onward must attach `standalone-dlssnr.addon64`, `nvngx.dll`, and `DLSS5_AIO_Feed.fx`. The NVIDIA runtime DLLs remain user-supplied and must not be attached to public releases.

Windowed mode is recommended at the desired render resolution, particularly while configuring the addon: it tends to expose the intended lower-resolution swapchain and gives the ReShade menu and processed preview separate screen space. Fullscreen and borderless remain supported where the game creates the expected backbuffer. A native-resolution game swapchain selects DLAA automatically; a lower-resolution swapchain selects DLSS Super Resolution. The addon keeps the desktop at native resolution, rejects auxiliary/helper swapchains, and presents the processed native output in its proxy window.

The game `OnPresent` event is the activation and evaluation boundary. The addon copies the reduced game backbuffer there and loads its own private `nvngx_dlssnr.dll`, `nvngx_dlss.dll`, and caller-identity bridge; it does not hook or reuse the game's DLSS implementation. RHI may deploy only the `.addon64` file to the game directory, so the addon also searches `%LOCALAPPDATA%\RHI\Custom\Addons` for the complete private runtime set.

Vulkan games must create a genuinely reduced swapchain. Use the game's windowed mode at the desired render resolution; the addon's native-size proxy supplies the borderless fullscreen output and scales proxy mouse coordinates back into the reduced game client. Do not override only `VkSwapchainCreateInfoKHR::imageExtent`: the application continues constructing framebuffers at the extent it originally requested, which device-loses No Man's Sky.

Vulkan frame copies run at ReShade's `reshade_finish_effects` boundary, matching the proven feeder transport: ReShade has transitioned the swapchain image to `render_target`, the addon temporarily moves it to `copy_source`, and then restores it before the Vulkan-to-D3D12 handoff. Direct copies from the `present` state device-loss No Man's Sky. At least one ReShade effect technique must be loaded so that the effects boundary executes; install the package's dependency-free `StandaloneBoundary.fx` and leave its no-op technique disabled when a game has no shader collection.

Version 1.7.5 leaves proxy-window mouse movement on the foreground game's native raw/relative-input path and forwards only buttons and wheel events. This prevents the scaled absolute-position feedback loop that caused cursor drift in No Man's Sky's reduced Vulkan window. Wheel coordinates are forwarded using their required screen-space convention. `Stable DLSS SR` now defaults to disabled; the game's own temporal anti-aliasing settings remain independent of the addon, while the stable mode remains available as a diagnostic override.

Version 1.7.6 mirrors the primary ReShade overlay into the native-size D3D12 ReShade runtime created on the proxy swapchain. This makes the menu visible after Vulkan's safe effects-boundary copy, which necessarily occurs before the primary runtime draws its overlay. While the menu is open, the proxy runtime consumes native-resolution mouse input directly; otherwise button forwarding and game-native raw movement remain unchanged. The addon also expands a reduced game's cursor clip to the visible proxy client every frame and releases it whenever the proxy or game loses focus.

Version 1.7.7 expands private-runtime discovery for non-RHI installations. Each DLL is resolved independently from the addon directory, game executable directory, process working directory, or `%LOCALAPPDATA%\RHI\Custom\Addons`, so the caller bridge may remain with the addon while NVIDIA runtimes live beside the game. Missing DLSS-G now disables only Frame Generation, and every searched candidate is written to the persistent log.

Version 1.7.8 removes the NVIDIA-driver-package-specific `nvmdi.inf` assumption from NGX core discovery. It scans all installed NVIDIA `nv*.inf_amd64_*` DriverStore packages for `_nvngx.dll`, supporting standard, DCH, and OEM INF names, and logs whether package enumeration or core discovery failed.

Version 1.7.9 avoids the optional NGX `UltraQuality` enum for DLSS Super Resolution feature creation. High input/output ratios now use the standard Quality preset, fixing `0xBAD00010 (UnsupportedParameter)` on drivers that otherwise initialize DLSS-NR successfully, including 2560x1080 to 3440x1440 ultrawide scaling.

Version 1.7.10 serializes native proxy initialization. Some injectors can re-enter or concurrently invoke Present while `CreateSwapChainForHwnd` is still running; previous builds could respond by creating multiple proxy threads and topmost windows before either swapchain became ready. Nested Presents now defer until the single in-progress proxy initialization completes.

Version 1.7.13 fixes mouse buttons being swallowed by the native presentation proxy. Gameplay retains the established proxy-to-game button forwarding and the addon no longer installs a global low-level mouse hook. If the proxy does not receive its own ReShade runtime, opening the ReShade menu temporarily hides the proxy so the native-sized game/ReShade window receives genuine Windows mouse input; closing the menu restores the DLSS output automatically. This removes the frozen duplicate cursor and makes ReShade controls clickable, with the temporary reduced-resolution menu view documented above.

Version 1.7.14 adds a persisted `Enable Neural Rendering` checkbox, enabled by default. Turning it off skips feature-18 evaluation while retaining DLSS Super Resolution and optional Frame Generation, providing an SR + FG-only presentation mode. Starting with NR disabled also skips creation of the feature-18 handle; enabling it live recreates the NGX feature set at the next Present when necessary.

Version 1.7.15 adds automatic DLAA selection for native-resolution games. When the game render dimensions exactly match the native output, the addon creates the NGX Super Sampling feature with `NVSDK_NGX_PerfQuality_Value_DLAA` and a 1:1 input/output contract. Lower resolutions continue selecting the existing DLSS Super Resolution quality modes. NR and optional Frame Generation remain available in either path, and the overlay/log now identify the active reconstruction mode explicitly.

Version 1.7.16 renames this addon's companion shader, technique, and guide resources to the `DLSS5_AIO_*` namespace. `DLSS5_AIO_Feed.fx` can coexist with the upstream DLSS5-Feeder project's `DLSS5_Feed.fx` without overwriting or binding it. The namespaced shader is included as a release asset beginning with this version and is part of the required asset checklist for future releases.

Version 1.7.17 adds an opt-in **Early proxy initialization (D3D11On12 compatibility)** mode for games that hang when the native proxy swapchain is first created from inside Present. When enabled before launch, D3D12 creates the proxy synchronously during primary effect-runtime initialization and keeps it hidden until its first completed output frame. It does not create a background initialization worker or add a global Present guard. The first primary Present validates the captured queue; a mismatch quarantines the hidden proxy and continues initializing the neural pipeline on the authoritative queue instead of submitting unsafe work. The setting defaults to disabled, applies only after restarting, and leaves the established D3D9, D3D11, Vulkan, and default D3D12 paths unchanged.

Version 1.7.18 fixes the three DLSS-NR model controls. The private 310.8 runtime selects its three effective neural variants through `DLSSNR.Style` values 0, 1, and 2, rather than through `DLSSNR.Hint.Render.Preset` alone. Model changes now publish both parameters during feature creation and every evaluation, while preserving the existing live feature-recreation behavior.

Version 1.7.22 moves native proxy presentation to a dedicated, bounded worker using a three-buffer frame-latency-waitable swapchain. Generated and real frames are paced independently from the game's Present callback, restoring Frame Generation output that v1.7.20's nonblocking safety path could discard whenever the proxy was busy. The game thread remains nonblocking, duplicate requests are coalesced, and finite GPU/swapchain waits fail open instead of hanging the game. This version also adds optional asynchronous CPU/GPU performance telemetry to the ReShade panel and persistent log.

Version 1.7.24 expands that performance telemetry to measure the VORT/feed guide passes and the asynchronous proxy compositor separately. It also corrects the experimental NR rejection-mask contract: a bound manual `ControlMask` now disables NVIDIA automatic masking, while strength zero unbinds the manual resource completely and returns to the same automatic-mask path used when the option is disabled. Nonzero strengths remain experimental because the private runtime currently treats the mask more like a hard gate than a smooth blend.

Version 1.7.21 removes the automatic Frame Generation block triggered by a loaded native Streamline module. Detection remains visible in the log and overlay, but the addon's normal Frame Generation checkbox is authoritative. Disable the game's built-in Frame Generation before enabling addon FG; running two Frame Generation implementations together is unsupported.

Version 1.7.20 makes presentation and resolution changes fail open instead of holding the game's Present thread. Ordinary neural and proxy frames never wait for unfinished GPU fences; busy work is skipped and counted in the overlay, while proxy DXGI presentation uses a nonblocking call. Resolution contracts must remain stable before NGX resources are created or replaced, and fullscreen/window mutations are deferred outside the DXGI callback. Games that load their own `sl.interposer` or `sl.dlss_g` presentation layer automatically safety-disable the addon's FG stage unless the user explicitly enables the unsafe expert override. This release also includes the optional VORT-driven NR rejection mask and makes F10 display a point-sampled raw pre-ReShade frame for a cleaner temporal diagnostic.

Version 1.4 uses `GetCapabilityParameters`, restores the snippet's provider callbacks after each parameter reset, and invokes NVIDIA's scaling-ratio callback during NR creation. Packed color and NR output are now allocated at the game's reduced render resolution; only the DLSS SR output is native-sized. This is the tested low-cost topology and removes the previous native-sized NR intermediates.

Version 1.5 adds an experimental direct-NGX DLSS Frame Generation stage. It evaluates feature 11 after the completed native DLSS SR frame and presents one generated frame followed by the current real frame through the existing proxy. The stage is enabled by default, has a live overlay toggle, warms up for two real frames, and automatically falls back to real-frame presentation if creation or evaluation fails. F10's stretched-original diagnostic deliberately remains single-present and bypasses frame generation. The standalone laboratory exposes the same path with `--framegen`; SDR and HDR probes require full generated-frame coverage and produce a checksum distinct from the real SR frame.

Version 1.5.1 supports reduced-resolution borderless presentation explicitly. It keeps the first full-size game HWND/runtime authoritative and ignores small secondary swapchains such as GTA V Enhanced's 176x44 D3D12 helper window, which previously displaced the real game runtime after a mode change. The proxy follows the primary monitor and uses a foreground/present watchdog: it hides when the game loses focus or stops presenting and restores only after the primary game window resumes, preventing a stale topmost black proxy from trapping the desktop.

Version 1.5.2 removes the prototype's serial double-VSync throttle. Generated and real frames now use separate D3D12 command allocators and are submitted through an uncapped flip-discard swapchain with tearing enabled when DXGI supports it. The game thread waits once for the preceding pair's GPU work rather than once between each image, so enabling FG no longer forces the source game toward a monitor-refresh divisor. The on-image counter reports proxy presents per second; the ReShade panel separately reports source FPS and proxy presents per second.

Version 1.6 adds native legacy-API transport. D3D11 games copy their backbuffer into a texture shared with the private D3D12 NGX device and synchronize it with a shared `ID3D11Fence`/`ID3D12Fence`. D3D9 games first copy into a D3D9 render target shared with the private D3D11 device, then enter the same fenced D3D11-to-D3D12 path. A second shared surface carries the post-ReShade frame to the D3D12 proxy compositor. Both routes preserve the existing compact NR -> native DLSS SR -> optional DLSS-G pipeline. The initial legacy implementation deliberately uses deterministic zero-motion/depth guides; D3D12 retains the same-frame VORT guide path.

Run `lab\build-legacy-smoke.bat` to validate both transports without a game. It selects the NVIDIA adapter and checks D3D11 shared texture/fence access followed by D3D9 -> D3D11 -> D3D12 sharing. On drivers that reject D3D12-created BGRA textures in D3D11, both the smoke test and addon automatically use the proven reverse D3D11-created NT-handle path.

Run `lab\build-vulkan-wsi-smoke.bat` to demonstrate the distinction between driver acceptance and application compatibility. The current driver accepts a reduced 1920x1080 swapchain for a 3840x2160 client, but that alone is not a supported integration: the game still believes its original extent and may build incompatible framebuffers.

Version 1.6.1 replaces the old global HDR10 default with per-game color-profile detection. `Auto` reads the primary swapchain color space and recognizes nonlinear sRGB/BT.709, linear BT.709/scRGB, BT.2100 PQ/HDR10, and BT.2100 HLG; older D3D9/D3D11 paths fall back to their surface format. The native proxy now uses a matching format and explicitly sets matching DXGI presentation metadata, so both neural output and F10 passthrough retain the game's color convention. Manual profile overrides remain available for games or wrappers that report incorrect metadata. The new `InputColorProfile` setting intentionally ignores the obsolete `ColorProfile` value, migrating existing installations that had been pinned to HDR10 back to Auto.

Version 1.7 adds 64-bit Windows Vulkan transport while retaining the verified private-D3D12 NGX and native proxy path. The addon creates its packed input and post-ReShade surfaces on D3D12 with shared NT handles, imports them into the game's `VkDevice` using `VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT`, and synchronizes both APIs with a shared D3D12 fence imported as a Vulkan timeline semaphore. Vulkan supplies the reduced pre/post-ReShade frames; NR, DLSS SR, optional DLSS-G, color conversion metadata, F10 presentation, and the native-size proxy remain on D3D12. Adapter LUID matching prevents cross-GPU sharing on hybrid systems, and resolution/format changes retire the imported Vulkan images with the existing NGX resource lifecycle.

Vulkan requires ReShade's 64-bit global implicit layer rather than a per-game `dxgi.dll`. The addon normally hooks `vkCreateDevice` early enough to enable the external-memory, external-semaphore, dedicated-allocation, and timeline-semaphore features itself. If `standalone-dlssnr.log` reports missing Vulkan interop entry points, use `addon\build\VulkanLayer\run-with-standalone-vulkan-layer.bat "path\to\game.exe"`; that per-launch fallback enables the same extensions without registering another global layer. It has no per-frame code. Native Linux/Proton and 32-bit Vulkan are not part of version 1.7.

Version 1.3 renders `vort_MotionEffects` and `DLSS5_AIO_Feed` explicitly inside the game `OnPresent` callback, then flushes that current-frame guide work before NGX evaluation. `DLSS5_AIO_Feed.fx` reads VORT's pooled `MotVectTexVort`, converts its delta-UV flow to pixel units, and also captures raw game depth. Both techniques remain disabled in ReShade's ordinary effect list because the addon schedules them itself in the required order. The overlay reports `same-frame VORT optical flow` only when those passes and correctly sized `R16G16_FLOAT` motion / `R32_FLOAT` depth resources are present; otherwise it reports and uses the internal zero-motion fallback.

Version 1.3.2 keeps that NGX evaluation at `OnPresent`, but defers the native proxy blit until ReShade's post-effects/post-overlay boundary. The stretched-original F10 view therefore contains ReShade's completed frame. In neural mode, the default `Composite ReShade effects/overlay` option compares that completed frame with the untouched pre-overlay input and carries changed pixels, including the FPS counter and ReShade menu, onto the neural output.

Version 1.3.3 makes the native proxy click-through while ReShade reports its menu open, so Windows delivers mouse input directly to Conan/ReShade rather than the proxy consuming it. It also draws an independent `FPS` counter directly in the native proxy shader; this is enabled by default and does not depend on ReShade's OSD settings.

Version 1.3.4 replaces passive click-through with an explicit low-level button bridge owned by the proxy thread. It activates only while ReShade's menu is open and Conan is the foreground window, routes button/wheel events to Conan's ReShade input window, and suppresses the duplicate proxy-targeted event. The compositor masks ReShade's one-frame-delayed software cursor at both its current and previous positions, leaving the Windows cursor as the single visible pointer.

The VORT shader provider must be installed in the game's ReShade shader search path alongside `DLSS5_AIO_Feed.fx`. On this Conan test installation it is deployed under `reshade-shaders\Shaders\VortShaders`, with the standard `ReShade.fxh` headers at the shader root. This fixes the previous event-ordering defect where ReShade rendered guides only after the addon's Present callback, so NGX consumed stale or zero motion even though its evaluate call returned success.

The persistent game log is `%LOCALAPPDATA%\RHI\Logs\standalone-dlssnr.log`. It reports runtime discovery, core/snippet initialization, both feature creation results, per-stage evaluation failures, the active input/output contract, and initial successful frames.

Color-profile changes require a game restart because they change the intermediate resource format and native proxy swapchain. The overlay reports the requested profile, the detected game swapchain color space and format, and the profile actually active in NGX/proxy presentation. Model 1/2/3 map to the runtime's effective Style 0/1/2 networks and are applied live: the addon waits for the prior neural frame, releases both NGX handles, and recreates feature 18 plus DLSS SR at the next game `Present`. The overlay reports the actually active model and style.

The `Enable Neural Rendering` checkbox keeps the same DLAA/DLSS Super Resolution, optional Frame Generation, and native proxy stages while routing the packed game color directly into reconstruction when disabled. Strength sliders apply when NR is enabled. F10 switches the native-size presentation window between processed output and a simple linear presentation of the original game backbuffer, so the comparison still fills the monitor. Home opens ReShade using the direct-input proxy bypass when necessary. Alt+X hides the proxy for NVIDIA's external overlay; after that, F10 restores the presentation window.

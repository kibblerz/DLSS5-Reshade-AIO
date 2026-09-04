# DLSS5 ReShade AIO

Bring Neural Rendering, DLAA/DLSS Super Resolution, and Frame Generation to supported 64-bit Windows games even when the game does not include those features. This is experimental software and currently supports D3D9, D3D11, D3D12, and Vulkan through ReShade.

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
2. Try fullscreen or borderless first. Windowed mode is also supported and can help games that refuse to create a reduced-resolution fullscreen image.
3. Select the game resolution:
   - **Same as the monitor:** the addon automatically uses **DLAA** at a 1:1 render scale.
   - **Lower than the monitor:** the addon uses **DLSS Super Resolution** to reconstruct the image to the monitor's native size.
4. If a lower game resolution still reports **DLAA**, the game is still presenting a native-size backbuffer. Switch between fullscreen, borderless, and windowed modes; restart after changing modes if necessary. Use whichever mode makes the overlay report **DLSS SR**.
5. Neural Rendering and Frame Generation are enabled by default and can be toggled independently in ReShade. Disabling both leaves an SR/DLAA-only pipeline.

Reduced-resolution DLSS SR can provide major performance improvements. Native-resolution DLAA instead prioritizes image quality.

- Press **F10** to compare the processed image with the original game output.
- The addon draws its own FPS counter because third-party overlays may not appear through its presentation proxy.
- Logs are written to `%LOCALAPPDATA%\RHI\Logs\standalone-dlssnr.log`.

## Quick troubleshooting

| Symptom | First things to try |
| --- | --- |
| Addon is missing from ReShade | Confirm ReShade has addon support, the game is 64-bit, and `standalone-dlssnr.addon64` is beside the actual game executable/ReShade DLL. |
| `required private runtime dependency missing` | Install `nvngx.dll` **as well as** the addon. Also place `nvngx_dlssnr.dll` and `nvngx_dlss.dll` beside them. `nvngx_dlssg.dll` is needed for Frame Generation. |
| Overlay reports fallback or zero-motion guides | Install `DLSS5_AIO_Feed.fx` under `reshade-shaders\Shaders` and install VORT Motion under the same shader search path. The addon remains usable without them, but temporal guidance is reduced. |
| Lower resolution still says DLAA | Switch between fullscreen, borderless, and windowed. The game must create a genuinely smaller backbuffer before DLSS SR can activate. Restart the game after changing resolution or display mode. |
| Processed image occupies only part of the screen, is confined to a corner, or remains game-window sized | Open **ReShade > Add-ons > Standalone DLSS-NR + SR**, expand **Compatibility / troubleshooting**, and enable **Force reduced-window virtualization**. This physically expands the reduced game window while preserving its lower rendering resolution. Restart if the game does not settle immediately. |
| Mouse hover/click position is offset, clicks only work in part of the screen, or the game detects the pointer somewhere else | Under **Compatibility / troubleshooting**, enable **Scale window input coordinates to render resolution**. It automatically enables the required **Force reduced-window virtualization** option. Disabling forced virtualization also disables its dependent input scaling. |
| Detached gameplay cursor remains visible and limits camera rotation at the screen edge | Current builds mirror the cursor state requested by the game, hiding it during relative camera control and restoring it for menus. If a game does not report that state correctly, try **Hide detached Windows cursor** under **Compatibility / troubleshooting** as a last resort. |
| Image is smeared or changes size after changing display settings | Restart the game. Set the resolution and display mode before loading gameplay. Press F10 once to confirm whether the presentation proxy is active. |
| ReShade menu temporarily makes the image smaller | Close the ReShade menu; the native-size processed output should return. This is a known proxy-input workaround in some games. |
| Black screen | Close the game, restore native resolution, and try another display mode. Do not repeatedly change resolution while the pipeline is active. Check the persistent log before trying again. |
| D3D12/D3D11On12 game hangs, freezes, or remains waiting for Present while starting | Try **Early proxy initialization (D3D11On12 compatibility)** using the instructions below. Leave it off for games that already start normally. |
| Native Streamline is detected | Disable the game's built-in DLSS Frame Generation. The addon no longer blocks its own FG stage merely because `sl.interposer` is loaded; running two FG implementations together is unsupported. |
| Vulkan says it is waiting for a shared frame | Make sure ReShade's Vulkan layer is active. Install `StandaloneBoundary.fx` when no other ReShade effect is loaded; Vulkan needs an effects boundary for the frame handoff. |

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
- Some games temporarily show their lower-resolution image while the ReShade menu is open.
- No Man's Sky and potentially other Vulkan games may not display the ReShade menu correctly.
- Additional game-specific and Vulkan issues are expected.
- The experimental VORT NR rejection mask currently behaves more like a hard gate than a gradual blend at nonzero strength. Leave it disabled unless testing this feature; strength zero is an exact bypass that restores NVIDIA automatic masking.

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

The game may be set to fullscreen or borderless at the desired render resolution. A native-resolution game swapchain selects DLAA automatically; a lower-resolution swapchain selects DLSS Super Resolution. The addon keeps the desktop at native resolution, rejects auxiliary/helper swapchains, and presents the processed native output in its proxy window.

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

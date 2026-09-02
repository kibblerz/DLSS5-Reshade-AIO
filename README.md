# Standalone DLSS-NR + Super Resolution prototype

This project contains two independently useful pieces:

- `lab/`: a deterministic D3D12 test program that reverse-engineers and validates the private DLSS-NR feature-18 contract without launching a game.
- `addon/`: a standalone ReShade addon for D3D9, D3D11, D3D12, and 64-bit Windows Vulkan games. It does not hook or depend on the ShortFuse addon.

## Proven pipeline

The laboratory established that this feature-18 package is a neural-rendering stage, not a spatial upscaler by itself. NVIDIA's own `DLSSNRComputeScalingRatioCallback` resolves every supported NR quality preset to `1.0`. In a direct 2x probe, feature 18 evaluates successfully but writes exactly the input-sized top-left quadrant (25% of the target). The working and efficient layout therefore gives NR render-sized color/output allocations, then gives only the downstream DLSS Super Resolution feature a native-sized output:

`low-resolution game color + depth + motion -> DLSS-NR feature 18 -> DLSS Super Resolution -> native presentation`

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

Expected summary:

```text
Matrix complete: 0 failing cases out of 9.
```

Each case writes a text log, a JSON result, and a PPM output. `nr-lab.log` contains the latest detailed run. A passing result requires successful feature-18 creation/evaluation, successful DLSS SR creation/evaluation, and over 95% changed-pixel coverage with every quadrant over 90%.

## Build and use the addon

Run `addon\build.bat`. The complete runtime set is emitted under `addon\build`:

- `standalone-dlssnr.addon64`
- `nvngx.dll` (the caller-identity bridge required by the NR snippet)
- `nvngx_dlssnr.dll` (the exact tested NR runtime)
- `nvngx_dlss.dll`
- `nvngx_dlssg.dll`
- `DLSS5_Feed.fx`

The game may be set to fullscreen or borderless at the desired lower render resolution. The addon keeps the desktop at native resolution, rejects auxiliary/helper swapchains, and presents the reconstructed native output in its proxy window.

The game `OnPresent` event is the activation and evaluation boundary. The addon copies the reduced game backbuffer there and loads its own private `nvngx_dlssnr.dll`, `nvngx_dlss.dll`, and caller-identity bridge; it does not hook or reuse the game's DLSS implementation. RHI may deploy only the `.addon64` file to the game directory, so the addon also searches `%LOCALAPPDATA%\RHI\Custom\Addons` for the complete private runtime set.

Vulkan games must create a genuinely reduced swapchain. Use the game's windowed mode at the desired render resolution; the addon's native-size proxy supplies the borderless fullscreen output and scales proxy mouse coordinates back into the reduced game client. Do not override only `VkSwapchainCreateInfoKHR::imageExtent`: the application continues constructing framebuffers at the extent it originally requested, which device-loses No Man's Sky.

Vulkan frame copies run at ReShade's `reshade_finish_effects` boundary, matching the proven feeder transport: ReShade has transitioned the swapchain image to `render_target`, the addon temporarily moves it to `copy_source`, and then restores it before the Vulkan-to-D3D12 handoff. Direct copies from the `present` state device-loss No Man's Sky. At least one ReShade effect technique must be loaded so that the effects boundary executes; install the package's dependency-free `StandaloneBoundary.fx` and leave its no-op technique disabled when a game has no shader collection.

Version 1.7.5 leaves proxy-window mouse movement on the foreground game's native raw/relative-input path and forwards only buttons and wheel events. This prevents the scaled absolute-position feedback loop that caused cursor drift in No Man's Sky's reduced Vulkan window. Wheel coordinates are forwarded using their required screen-space convention. `Stable DLSS SR` now defaults to disabled; the game's own temporal anti-aliasing settings remain independent of the addon, while the stable mode remains available as a diagnostic override.

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

Version 1.3 renders `vort_MotionEffects` and `DLSS5_Feed` explicitly inside the game `OnPresent` callback, then flushes that current-frame guide work before NGX evaluation. `DLSS5_Feed.fx` reads VORT's pooled `MotVectTexVort`, converts its delta-UV flow to pixel units, and also captures raw game depth. Both techniques remain disabled in ReShade's ordinary effect list because the addon schedules them itself in the required order. The overlay reports `same-frame VORT optical flow` only when those passes and correctly sized `R16G16_FLOAT` motion / `R32_FLOAT` depth resources are present; otherwise it reports and uses the internal zero-motion fallback.

Version 1.3.2 keeps that NGX evaluation at `OnPresent`, but defers the native proxy blit until ReShade's post-effects/post-overlay boundary. The stretched-original F10 view therefore contains ReShade's completed frame. In neural mode, the default `Composite ReShade effects/overlay` option compares that completed frame with the untouched pre-overlay input and carries changed pixels, including the FPS counter and ReShade menu, onto the neural output.

Version 1.3.3 makes the native proxy click-through while ReShade reports its menu open, so Windows delivers mouse input directly to Conan/ReShade rather than the proxy consuming it. It also draws an independent `FPS` counter directly in the native proxy shader; this is enabled by default and does not depend on ReShade's OSD settings.

Version 1.3.4 replaces passive click-through with an explicit low-level button bridge owned by the proxy thread. It activates only while ReShade's menu is open and Conan is the foreground window, routes button/wheel events to Conan's ReShade input window, and suppresses the duplicate proxy-targeted event. The compositor masks ReShade's one-frame-delayed software cursor at both its current and previous positions, leaving the Windows cursor as the single visible pointer.

The VORT shader provider must be installed in the game's ReShade shader search path alongside `DLSS5_Feed.fx`. On this Conan test installation it is deployed under `reshade-shaders\Shaders\VortShaders`, with the standard `ReShade.fxh` headers at the shader root. This fixes the previous event-ordering defect where ReShade rendered guides only after the addon's Present callback, so NGX consumed stale or zero motion even though its evaluate call returned success.

The persistent game log is `%LOCALAPPDATA%\RHI\Logs\standalone-dlssnr.log`. It reports runtime discovery, core/snippet initialization, both feature creation results, per-stage evaluation failures, the active input/output contract, and initial successful frames.

Color-profile changes require a game restart because they change the intermediate resource format and native proxy swapchain. The overlay reports the requested profile, the detected game swapchain color space and format, and the profile actually active in NGX/proxy presentation. Model 1/2/3 changes are applied live: the addon waits for the prior neural frame, releases both NGX handles, and recreates feature 18 plus DLSS SR at the next game `Present`. The overlay reports the actually active model.

The `Diagnostic A/B: bypass feature 18` checkbox keeps the same DLSS Super Resolution stage and native proxy while routing the packed game color directly into SR. This provides an in-game NR-on versus NR-bypassed comparison instead of treating a successful NGX return code as proof of a materially different image. Strength sliders are applied live. F10 switches the native-size presentation window between neural output and a simple linear stretch of the original reduced-resolution game backbuffer, so the non-neural comparison still fills the monitor. Home keeps the proxy visible and shows ReShade through the post-overlay compositor. Alt+X hides the proxy for NVIDIA's external overlay; after that, F10 restores the presentation window.

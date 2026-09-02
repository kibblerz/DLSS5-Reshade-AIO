# Standalone DLSS-NR + Super Resolution prototype

This project contains two independently useful pieces:

- `lab/`: a deterministic D3D12 test program that reverse-engineers and validates the private DLSS-NR feature-18 contract without launching a game.
- `addon/`: a standalone ReShade addon for the D3D12 game path. It does not hook or depend on the ShortFuse addon.

## Proven pipeline

The laboratory established that feature 18 is a neural-rendering stage, not a full spatial upscaler by itself. Its color and output allocations must both be native-sized, while the lower-resolution input image is packed into the top-left and described by the private `DLSSNR.*Subrect*` parameters. Feature 18 updates that active region. A second standard DLSS Super Resolution feature consumes the NR result and reconstructs the native frame:

`low-resolution game color + depth + motion -> DLSS-NR feature 18 -> DLSS Super Resolution -> native presentation`

The matrix validates all three input profiles and all three NR models. It also verifies full output coverage and verifies that changing NR intensity changes the final native-output checksum, which demonstrates that NR is materially participating rather than being bypassed.

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
- `DLSS5_Feed.fx`

Conan should be set to fullscreen at the desired lower render resolution. The addon virtualizes the fullscreen transition so the desktop stays at native resolution, then presents the reconstructed native output in its proxy window.

The game `OnPresent` event is the activation and evaluation boundary. The addon copies the reduced game backbuffer there and loads its own private `nvngx_dlssnr.dll`, `nvngx_dlss.dll`, and caller-identity bridge; it does not hook or reuse the game's DLSS implementation. RHI may deploy only the `.addon64` file to the game directory, so the addon also searches `%LOCALAPPDATA%\RHI\Custom\Addons` for the complete private runtime set.

Version 1.3 renders `vort_MotionEffects` and `DLSS5_Feed` explicitly inside the game `OnPresent` callback, then flushes that current-frame guide work before NGX evaluation. `DLSS5_Feed.fx` reads VORT's pooled `MotVectTexVort`, converts its delta-UV flow to pixel units, and also captures raw game depth. Both techniques remain disabled in ReShade's ordinary effect list because the addon schedules them itself in the required order. The overlay reports `same-frame VORT optical flow` only when those passes and correctly sized `R16G16_FLOAT` motion / `R32_FLOAT` depth resources are present; otherwise it reports and uses the internal zero-motion fallback.

Version 1.3.2 keeps that NGX evaluation at `OnPresent`, but defers the native proxy blit until ReShade's post-effects/post-overlay boundary. The stretched-original F10 view therefore contains ReShade's completed frame. In neural mode, the default `Composite ReShade effects/overlay` option compares that completed frame with the untouched pre-overlay input and carries changed pixels, including the FPS counter and ReShade menu, onto the neural output.

Version 1.3.3 makes the native proxy click-through while ReShade reports its menu open, so Windows delivers mouse input directly to Conan/ReShade rather than the proxy consuming it. It also draws an independent `FPS` counter directly in the native proxy shader; this is enabled by default and does not depend on ReShade's OSD settings.

Version 1.3.4 replaces passive click-through with an explicit low-level button bridge owned by the proxy thread. It activates only while ReShade's menu is open and Conan is the foreground window, routes button/wheel events to Conan's ReShade input window, and suppresses the duplicate proxy-targeted event. The compositor masks ReShade's one-frame-delayed software cursor at both its current and previous positions, leaving the Windows cursor as the single visible pointer.

The VORT shader provider must be installed in the game's ReShade shader search path alongside `DLSS5_Feed.fx`. On this Conan test installation it is deployed under `reshade-shaders\Shaders\VortShaders`, with the standard `ReShade.fxh` headers at the shader root. This fixes the previous event-ordering defect where ReShade rendered guides only after the addon's Present callback, so NGX consumed stale or zero motion even though its evaluate call returned success.

The persistent game log is `%LOCALAPPDATA%\RHI\Logs\standalone-dlssnr.log`. It reports runtime discovery, core/snippet initialization, both feature creation results, per-stage evaluation failures, the active input/output contract, and initial successful frames.

Color-profile changes require a game restart because they change the intermediate resource format. Model 1/2/3 changes are applied live: the addon waits for the prior neural frame, releases both NGX handles, and recreates feature 18 plus DLSS SR at the next game `Present`. The overlay reports the actually active model.

The `Diagnostic A/B: bypass feature 18` checkbox keeps the same DLSS Super Resolution stage and native proxy while routing the packed game color directly into SR. This provides an in-game NR-on versus NR-bypassed comparison instead of treating a successful NGX return code as proof of a materially different image. Strength sliders are applied live. F10 switches the native-size presentation window between neural output and a simple linear stretch of the original reduced-resolution game backbuffer, so the non-neural comparison still fills the monitor. Home keeps the proxy visible and shows ReShade through the post-overlay compositor. Alt+X hides the proxy for NVIDIA's external overlay; after that, F10 restores the presentation window.

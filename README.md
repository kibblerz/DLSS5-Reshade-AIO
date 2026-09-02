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

`DLSS5_Feed.fx` is now optional. When its correctly sized `R16G16_FLOAT` motion and `R32_FLOAT` depth resources are available, the addon captures them for the next `OnPresent`. Otherwise it creates and clears internal fallback guides so initialization and native reconstruction are not blocked. The fallback proves the standalone path and maintains full-frame output, though true game motion/depth will ultimately be needed for high-quality temporal reconstruction in motion.

The persistent game log is `%LOCALAPPDATA%\RHI\Logs\standalone-dlssnr.log`. It reports runtime discovery, core/snippet initialization, both feature creation results, per-stage evaluation failures, the active input/output contract, and initial successful frames.

Color-profile changes require a game restart because they change the intermediate resource format. Model 1/2/3 changes are applied live: the addon waits for the prior neural frame, releases both NGX handles, and recreates feature 18 plus DLSS SR at the next game `Present`. The overlay reports the actually active model.

The `Diagnostic A/B: bypass feature 18` checkbox keeps the same DLSS Super Resolution stage and native proxy while routing the packed game color directly into SR. This provides an in-game NR-on versus NR-bypassed comparison instead of treating a successful NGX return code as proof of a materially different image. Strength sliders are applied live. F10 hides or shows the native proxy; Home and Alt+X hide it so ReShade or NVIDIA overlays can receive input.

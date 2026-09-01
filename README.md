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

Conan should be set to fullscreen at the desired lower render resolution. The addon virtualizes the fullscreen transition so the desktop stays at native resolution, then presents the reconstructed native output in its proxy window. `DLSS5_Feed.fx` and a compatible motion-vector provider must be available and the `DLSS5_Feed` technique must be enabled.

The persistent game log is `%LOCALAPPDATA%\RHI\Logs\standalone-dlssnr.log`. It reports runtime discovery, core/snippet initialization, both feature creation results, per-stage evaluation failures, the active input/output contract, and initial successful frames.

Color profile and model changes require a game restart because they affect feature creation. Strength sliders are applied live. F10 hides or shows the native proxy; Home and Alt+X hide it so ReShade or NVIDIA overlays can receive input.

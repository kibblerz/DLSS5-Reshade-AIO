# Third-party notices

The Apache License 2.0 in this repository applies to the original DLSS5 ReShade AIO code and documentation contributed by its copyright holders. It does not relicense third-party software, SDK material, submodules, or proprietary runtime binaries. Those components remain subject to their respective licenses and terms.

## DLSS5-Feeder and bundled dependencies

The `external/DLSS5-Feeder` Git submodule is developed by Jean-Laurent ROUZIES and is licensed separately under the MIT License. It includes portions attributed to NIGos and dependencies with their own license files, including ReShade, ImGui, MinHook, RenoDX, Microsoft Detours, and Deep-Fried-Chicken components. Preserve the notices shipped in that submodule when redistributing any of its source or binary material.

- [`external/DLSS5-Feeder/LICENSE`](external/DLSS5-Feeder/LICENSE)
- [`external/DLSS5-Feeder`](https://github.com/jlrouzies-fr/DLSS5-Feeder)

## NVIDIA software

NVIDIA NGX, DLSS, DLSS Frame Generation, and DLSS Neural Rendering runtime binaries and related SDK materials are NVIDIA software governed by NVIDIA's applicable license terms. They are not licensed under this project's Apache License 2.0. The public project and its releases do not redistribute the private NVIDIA runtime DLL set; users must obtain required NVIDIA files from a source whose terms permit their use.

NVIDIA, DLSS, and related product names are trademarks or registered trademarks of NVIDIA Corporation. This project is independent and is not endorsed by NVIDIA.

The experimental NVIDIA Optical Flow motion provider vendors the public NVIDIA Optical Flow SDK 5.0 interface headers `nvOpticalFlowCommon.h` and `nvOpticalFlowD3D12.h`. Their original copyright and permissive license notices are retained in each header. At runtime, the provider dynamically loads the Optical Flow API installed by the NVIDIA display driver; this repository does not redistribute that driver library.

- [`NVIDIA Optical Flow SDK`](https://developer.nvidia.com/opticalflow-sdk)
- [`NVIDIA Optical Flow documentation`](https://docs.nvidia.com/video-technologies/optical-flow-sdk/)

## ReShade

ReShade is developed by Patrick Mours and contributors and is governed by its own license. This project is a ReShade addon and is not affiliated with or endorsed by the ReShade project. Relevant ReShade notices are retained in the dependency tree.

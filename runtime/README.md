# Local NVIDIA runtimes

This directory intentionally contains no binaries. Obtain the following runtime files from sources whose licenses permit your use and place them here before packaging or running the laboratory:

- `nvngx_dlssnr.dll`
- `nvngx_dlss.dll`
- `nvngx_dlssg.dll`

The addon build succeeds without these files but reports which runtime files were omitted. The validation laboratory requires all three. Do not commit or redistribute them from this repository.

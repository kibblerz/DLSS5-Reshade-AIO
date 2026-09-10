## DLSS5 ReShade AIO v2.2.3

This release adds 360p and 180p NVIDIA Optical Flow working resolutions and makes aspect-preserving 180p the Auto default.

Uncapped Conan Exiles testing reduced observed Optical Flow submit-to-dispatch latency from roughly 37 ms at 720p to 12 ms at 180p, while nearly eliminating NVOF backpressure and retaining most of the motion-stabilization benefit. Higher resolutions remain selectable when additional thin-object precision is preferred.

Both 32-bit and 64-bit packages contain the updated processing pipeline. ReShade and NVIDIA runtime DLLs remain user-supplied.

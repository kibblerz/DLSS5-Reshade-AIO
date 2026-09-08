/*
    Renderer-selecting wrapper for the upstream DLSS5-Feeder effect.

    Classic D3D9 uses DLSS5_Feed_D3D9.fx as its lightweight capture trigger.
    The upstream motion-validation effect exceeds shader model 3 limits, so do
    not include or compile it on D3D9. D3D10/11, OpenGL and Vulkan retain the
    upstream implementation without maintaining a second modified copy.
*/

#if !defined(__RENDERER__) || __RENDERER__ != 0x9000
    #include "DLSS5_Feed_impl.fxh"
#endif

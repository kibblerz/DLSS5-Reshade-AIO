/*
    Native x86 D3D9 trigger for the AIO bridge.

    D3D9 frames currently enter the x64 pipeline with deterministic neutral
    motion/depth guides, so the large optical-flow validation shader used by
    D3D11 is unnecessary here (and exceeds the D3D9 shader compiler limits).
    This identity pass gives the addon a precise post-effects callback without
    changing the game's image.
*/

#include "ReShade.fxh"

texture DLSS5_D3D9_Color : COLOR;
sampler sDLSS5_D3D9_Color
{
    Texture = DLSS5_D3D9_Color;
    AddressU = Clamp;
    AddressV = Clamp;
    MinFilter = Point;
    MagFilter = Point;
    MipFilter = Point;
};

float4 PS_DLSS5_D3D9_Identity(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    return tex2D(sDLSS5_D3D9_Color, texcoord);
}

technique DLSS5_Feed
<
    ui_label = "DLSS 5 AIO native D3D9 capture";
    ui_tooltip = "Lightweight identity trigger used by the native 32-bit D3D9 bridge.";
>
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader = PS_DLSS5_D3D9_Identity;
    }
}

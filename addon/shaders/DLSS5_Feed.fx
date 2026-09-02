/*
    Standalone guide capture for the DLSS-NR OnPresent addon.

    This intentionally has no external include or motion-provider dependency, so
    RHI can install it into an otherwise empty ReShade shader directory. It gives
    the addon the game's real raw depth. Motion remains zero until a native game
    motion-vector capture path is added; the addon reports that limitation rather
    than pretending the fallback is production-quality temporal input.
*/

texture DLSS5_GameDepth : DEPTH;
sampler sDLSS5_GameDepth
{
    Texture = DLSS5_GameDepth;
    AddressU = Clamp;
    AddressV = Clamp;
    MipFilter = Point;
    MinFilter = Point;
    MagFilter = Point;
};

texture DLSS5_MV
{
    Width = BUFFER_WIDTH;
    Height = BUFFER_HEIGHT;
    Format = RG16F;
};

texture DLSS5_Depth
{
    Width = BUFFER_WIDTH;
    Height = BUFFER_HEIGHT;
    Format = R32F;
};

texture DLSS5_Mask
{
    Width = BUFFER_WIDTH;
    Height = BUFFER_HEIGHT;
    Format = R8;
};

void DLSS5_FullscreenVS(uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD)
{
    texcoord = float2((id << 1) & 2, id & 2);
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

void DLSS5_CaptureGuides(float4 position : SV_Position, float2 texcoord : TEXCOORD,
    out float2 motion : SV_Target0, out float depth : SV_Target1, out float mask : SV_Target2)
{
    motion = 0.0;
    depth = tex2Dlod(sDLSS5_GameDepth, float4(texcoord, 0.0, 0.0)).x;
    mask = 0.0;
}

technique DLSS5_Feed
<
    ui_label = "Standalone DLSS-NR guides (real depth, zero motion)";
    ui_tooltip = "Supplies raw game depth to the standalone OnPresent pipeline. Motion is deliberately zero until a native motion-vector capture is available.";
>
{
    pass
    {
        VertexShader = DLSS5_FullscreenVS;
        PixelShader = DLSS5_CaptureGuides;
        RenderTarget0 = DLSS5_MV;
        RenderTarget1 = DLSS5_Depth;
        RenderTarget2 = DLSS5_Mask;
    }
}

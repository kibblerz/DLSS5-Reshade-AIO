/*
    Standalone guide capture for the DLSS-NR OnPresent addon.

    VORT's technique is rendered explicitly by the addon inside the Present
    callback before this pass. ReShade pools MotVectTexVort by name, so this
    effect consumes current-frame optical flow and converts delta UV to the pixel
    units expected by NGX. It also supplies the game's real raw depth.
*/

texture2D MotVectTexVort
{
    Width = BUFFER_WIDTH;
    Height = BUFFER_HEIGHT;
    Format = RG16F;
};
sampler sMotVectTexVort
{
    Texture = MotVectTexVort;
    AddressU = Clamp;
    AddressV = Clamp;
    MipFilter = Point;
    MinFilter = Point;
    MagFilter = Point;
};

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
    // VORT publishes previous_uv = current_uv + motion. DLSS uses the same
    // direction but expects pixels rather than normalized UV units.
    motion = tex2Dlod(sMotVectTexVort, float4(texcoord, 0.0, 0.0)).xy *
        float2(BUFFER_WIDTH, BUFFER_HEIGHT);
    depth = tex2Dlod(sDLSS5_GameDepth, float4(texcoord, 0.0, 0.0)).x;
    mask = 0.0;
}

technique DLSS5_Feed
<
    ui_label = "Standalone DLSS-NR guides (same-frame VORT motion + depth)";
    ui_tooltip = "Rendered manually at Present after VORT, before the standalone NGX pipeline.";
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

/*
    DLSS5 ReShade AIO guide capture for the standalone OnPresent addon.

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

texture DLSS5_AIO_GameDepth : DEPTH;
sampler sDLSS5_AIO_GameDepth
{
    Texture = DLSS5_AIO_GameDepth;
    AddressU = Clamp;
    AddressV = Clamp;
    MipFilter = Point;
    MinFilter = Point;
    MagFilter = Point;
};

texture DLSS5_AIO_MV
{
    Width = BUFFER_WIDTH;
    Height = BUFFER_HEIGHT;
    Format = RG16F;
};

texture DLSS5_AIO_Depth
{
    Width = BUFFER_WIDTH;
    Height = BUFFER_HEIGHT;
    Format = R32F;
};

texture DLSS5_AIO_Mask
{
    Width = BUFFER_WIDTH;
    Height = BUFFER_HEIGHT;
    Format = R8;
};

texture DLSS5_AIO_NRMask
{
    Width = BUFFER_WIDTH;
    Height = BUFFER_HEIGHT;
    Format = R8;
};

uniform float DLSS5_AIO_NRMaskStrength
<
    hidden = true;
> = 1.0;

void DLSS5_AIO_FullscreenVS(uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD)
{
    texcoord = float2((id << 1) & 2, id & 2);
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

void DLSS5_AIO_CaptureGuides(float4 position : SV_Position, float2 texcoord : TEXCOORD,
    out float2 motion : SV_Target0, out float depth : SV_Target1, out float mask : SV_Target2,
    out float nr_mask : SV_Target3)
{
    // VORT publishes previous_uv = current_uv + motion. DLSS uses the same
    // direction but expects pixels rather than normalized UV units.
    float2 motion_uv = tex2Dlod(sMotVectTexVort, float4(texcoord, 0.0, 0.0)).xy;
    motion = motion_uv * float2(BUFFER_WIDTH, BUFFER_HEIGHT);
    depth = tex2Dlod(sDLSS5_AIO_GameDepth, float4(texcoord, 0.0, 0.0)).x;

    // VORT is screen-space optical flow, so it cannot provide reliable history
    // at newly exposed pixels, outside-screen reprojections, or hard depth
    // boundaries. Mark those pixels for current-frame bias in DLSS instead of
    // allowing a bad warp to persist as a trail.
    float2 previous_uv = texcoord + motion_uv;
    float outside = any(previous_uv < 0.0) || any(previous_uv > 1.0) ? 1.0 : 0.0;
    float extreme_flow = smoothstep(48.0, 160.0, length(motion));
    float2 pixel = 1.0 / float2(BUFFER_WIDTH, BUFFER_HEIGHT);
    float depth_dx = abs(depth - tex2Dlod(sDLSS5_AIO_GameDepth, float4(texcoord + float2(pixel.x, 0.0), 0.0, 0.0)).x);
    float depth_dy = abs(depth - tex2Dlod(sDLSS5_AIO_GameDepth, float4(texcoord + float2(0.0, pixel.y), 0.0, 0.0)).x);
    float relative_depth_edge = max(depth_dx, depth_dy) / max(abs(depth), 1e-4);
    float depth_edge = smoothstep(0.015, 0.08, relative_depth_edge);
    mask = saturate(max(outside, max(extreme_flow, depth_edge)));

    // DLSS SR interprets one as "prefer the current frame", while feature-18's
    // ControlMask uses the opposite convention: one applies NR and zero
    // bypasses it. Keep both forms so each stage receives its native polarity.
    nr_mask = saturate(1.0 - mask * DLSS5_AIO_NRMaskStrength);
}

technique DLSS5_AIO_Feed
<
    ui_label = "DLSS5 ReShade AIO guides (same-frame VORT motion + depth)";
    ui_tooltip = "Rendered manually at Present after VORT, before the DLSS5 ReShade AIO pipeline.";
>
{
    pass
    {
        VertexShader = DLSS5_AIO_FullscreenVS;
        PixelShader = DLSS5_AIO_CaptureGuides;
        RenderTarget0 = DLSS5_AIO_MV;
        RenderTarget1 = DLSS5_AIO_Depth;
        RenderTarget2 = DLSS5_AIO_Mask;
        RenderTarget3 = DLSS5_AIO_NRMask;
    }
}

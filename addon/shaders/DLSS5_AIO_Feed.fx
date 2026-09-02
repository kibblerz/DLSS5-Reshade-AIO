/*
    DLSS5 ReShade AIO guide capture for the standalone OnPresent addon.

    The addon renders VORT immediately before this technique. In addition to
    converting VORT's delta-UV flow to the pixel units expected by NGX, this
    effect validates that flow against previous-frame luma, linear depth and
    motion history. Distrusted history is rejected before it can become a
    persistent DLSS trail. The addon invokes this technique only when the VORT
    technique and every required guide resource are available.
*/

#include "ReShade.fxh"

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

texture DLSS5_AIO_GameColor : COLOR;
sampler sDLSS5_AIO_GameColor
{
    Texture = DLSS5_AIO_GameColor;
    AddressU = Clamp;
    AddressV = Clamp;
    MipFilter = Point;
    MinFilter = Point;
    MagFilter = Point;
};

// Controlled by the addon's checkbox. These are deliberately hidden from the
// ordinary shader UI so there is only one authoritative setting.
uniform bool DLSS5_AIO_EnableAdaptiveRejection < hidden = true; > = true;
uniform bool DLSS5_AIO_ResetAdaptiveHistory < hidden = true; > = true;

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

// Responsivity uses R16F because NVIDIA's DLSSD contract accepts R16F or
// R8_SNORM in the API range. ReShade's plain R8 format is UNORM, so it is kept
// only for the separate DLSS disocclusion mask.
texture DLSS5_AIO_RawMask
{
    Width = BUFFER_WIDTH;
    Height = BUFFER_HEIGHT;
    Format = R16F;
};
sampler sDLSS5_AIO_RawMask
{
    Texture = DLSS5_AIO_RawMask;
    AddressU = Clamp;
    AddressV = Clamp;
    MipFilter = Point;
    MinFilter = Point;
    MagFilter = Point;
};

texture DLSS5_AIO_Mask
{
    Width = BUFFER_WIDTH;
    Height = BUFFER_HEIGHT;
    Format = R16F;
};

texture DLSS5_AIO_RawDisocclusionMask
{
    Width = BUFFER_WIDTH;
    Height = BUFFER_HEIGHT;
    Format = R8;
};
sampler sDLSS5_AIO_RawDisocclusionMask
{
    Texture = DLSS5_AIO_RawDisocclusionMask;
    AddressU = Clamp;
    AddressV = Clamp;
    MipFilter = Point;
    MinFilter = Point;
    MagFilter = Point;
};

texture DLSS5_AIO_DisocclusionMask
{
    Width = BUFFER_WIDTH;
    Height = BUFFER_HEIGHT;
    Format = R8;
};

// Previous-frame history. The final pass updates these only after the current
// frame has consumed them, so the guide pass always compares adjacent frames.
texture DLSS5_AIO_PrevLuma
{
    Width = BUFFER_WIDTH;
    Height = BUFFER_HEIGHT;
    Format = R16F;
};
sampler sDLSS5_AIO_PrevLuma
{
    Texture = DLSS5_AIO_PrevLuma;
    AddressU = Clamp;
    AddressV = Clamp;
    MipFilter = Point;
    MinFilter = Linear;
    MagFilter = Linear;
};

texture DLSS5_AIO_PrevDepth
{
    Width = BUFFER_WIDTH;
    Height = BUFFER_HEIGHT;
    Format = R16F;
};
sampler sDLSS5_AIO_PrevDepth
{
    Texture = DLSS5_AIO_PrevDepth;
    AddressU = Clamp;
    AddressV = Clamp;
    MipFilter = Point;
    MinFilter = Point;
    MagFilter = Point;
};

texture DLSS5_AIO_PrevMV
{
    Width = BUFFER_WIDTH;
    Height = BUFFER_HEIGHT;
    Format = RG16F;
};
sampler sDLSS5_AIO_PrevMV
{
    Texture = DLSS5_AIO_PrevMV;
    AddressU = Clamp;
    AddressV = Clamp;
    MipFilter = Point;
    MinFilter = Point;
    MagFilter = Point;
};

void DLSS5_AIO_FullscreenVS(uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD)
{
    texcoord = float2((id << 1) & 2, id & 2);
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float DLSS5_AIO_Luma(float2 uv)
{
    return dot(tex2Dlod(sDLSS5_AIO_GameColor, float4(uv, 0.0, 0.0)).rgb,
        float3(0.299, 0.587, 0.114));
}

float DLSS5_AIO_RawDepth(float2 uv)
{
    // Apply the same orientation, scale and offset corrections as ReShade's
    // linear-depth helper, but preserve raw hardware depth for NGX.
    float2 depth_uv = uv;
#if RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN
    depth_uv.y = 1.0 - depth_uv.y;
#endif
    depth_uv.x /= RESHADE_DEPTH_INPUT_X_SCALE;
    depth_uv.y /= RESHADE_DEPTH_INPUT_Y_SCALE;
#if RESHADE_DEPTH_INPUT_X_PIXEL_OFFSET
    depth_uv.x -= RESHADE_DEPTH_INPUT_X_PIXEL_OFFSET * BUFFER_RCP_WIDTH;
#else
    depth_uv.x -= RESHADE_DEPTH_INPUT_X_OFFSET / 2.000000001;
#endif
#if RESHADE_DEPTH_INPUT_Y_PIXEL_OFFSET
    depth_uv.y += RESHADE_DEPTH_INPUT_Y_PIXEL_OFFSET * BUFFER_RCP_HEIGHT;
#else
    depth_uv.y += RESHADE_DEPTH_INPUT_Y_OFFSET / 2.000000001;
#endif
    return tex2Dlod(ReShade::DepthBuffer, float4(depth_uv, 0.0, 0.0)).x;
}

// x = appearance mismatch, y = depth mismatch, z = vector inconsistency.
float4 DLSS5_AIO_ValidateMotion(float2 uv, float2 motion_uv)
{
    float4 failure = float4(0.0, 0.0, 0.0, 0.0);
    if (!DLSS5_AIO_EnableAdaptiveRejection || DLSS5_AIO_ResetAdaptiveHistory)
        return failure;

    const float2 previous_uv = uv + motion_uv;
    if (any(previous_uv < 0.0) || any(previous_uv > 1.0))
        return float4(1.0, 0.0, 0.0, 0.0);

    const float motion_pixels = length(motion_uv * float2(BUFFER_WIDTH, BUFFER_HEIGHT));

    // A previous luma value outside the current 3x3 neighbourhood is unlikely
    // to belong to this surface. Keep the vector, but ask DLSS for current color.
    const float2 pixel = 1.0 / float2(BUFFER_WIDTH, BUFFER_HEIGHT);
    float current_luma = DLSS5_AIO_Luma(uv);
    float luma_min = current_luma;
    float luma_max = current_luma;
    [unroll] for (int y = -1; y <= 1; ++y)
    [unroll] for (int x = -1; x <= 1; ++x)
    {
        const float sample_luma = DLSS5_AIO_Luma(uv + float2(x, y) * pixel);
        luma_min = min(luma_min, sample_luma);
        luma_max = max(luma_max, sample_luma);
    }
    const float previous_luma = tex2Dlod(sDLSS5_AIO_PrevLuma,
        float4(previous_uv, 0.0, 0.0)).x;
    const float luma_scale = max(max(abs(luma_min), abs(luma_max)), 0.05);
    const float luma_margin = 0.25 * luma_scale + 2.0 / 255.0;
    failure.x = saturate(max(luma_min - previous_luma, previous_luma - luma_max) / luma_margin);

    // Linear-depth disagreement identifies disocclusions. Sky/far-plane pixels
    // are exempt because their depth is commonly cleared or unavailable.
    const float current_depth = ReShade::GetLinearizedDepth(uv);
    if (current_depth < 0.999)
    {
        const float previous_depth = tex2Dlod(sDLSS5_AIO_PrevDepth,
            float4(previous_uv, 0.0, 0.0)).x;
        const float depth_tolerance = 0.10 * max(current_depth, 1e-3);
        failure.y = saturate((abs(previous_depth - current_depth) - depth_tolerance) /
            (depth_tolerance + 1e-5));
    }

    // Real surface motion normally changes smoothly. Large disagreement with
    // the previous vector at the reprojected location is treated as bad flow.
    const float2 previous_motion = tex2Dlod(sDLSS5_AIO_PrevMV,
        float4(previous_uv, 0.0, 0.0)).xy;
    const float vector_difference = length((motion_uv - previous_motion) *
        float2(BUFFER_WIDTH, BUFFER_HEIGHT));
    const float allowed_difference = 1.4 + 0.5 * motion_pixels;
    failure.z = saturate((vector_difference - allowed_difference) /
        max(allowed_difference, 1e-5));
    return failure;
}

void DLSS5_AIO_CaptureGuides(float4 position : SV_Position, float2 texcoord : TEXCOORD,
    out float2 motion : SV_Target0, out float depth : SV_Target1,
    out float responsivity : SV_Target2, out float disocclusion : SV_Target3)
{
    // VORT publishes previous_uv = current_uv + motion. DLSS uses the same
    // direction but expects pixels rather than normalized UV units.
    float2 motion_uv = tex2Dlod(sMotVectTexVort, float4(texcoord, 0.0, 0.0)).xy;
    const float4 validation = DLSS5_AIO_ValidateMotion(texcoord, motion_uv);
    const float zero_vector = max(validation.y, validation.z);
    motion_uv *= 1.0 - zero_vector;
    motion = motion_uv * float2(BUFFER_WIDTH, BUFFER_HEIGHT);
    depth = DLSS5_AIO_RawDepth(texcoord);

    const float2 previous_uv = texcoord + motion_uv;
    const float outside = any(previous_uv < 0.0) || any(previous_uv > 1.0) ? 1.0 : 0.0;
    const float extreme_flow = smoothstep(48.0, 160.0, length(motion));
    const float2 pixel = 1.0 / float2(BUFFER_WIDTH, BUFFER_HEIGHT);
    const float depth_dx = abs(depth - DLSS5_AIO_RawDepth(texcoord + float2(pixel.x, 0.0)));
    const float depth_dy = abs(depth - DLSS5_AIO_RawDepth(texcoord + float2(0.0, pixel.y)));
    const float relative_depth_edge = max(depth_dx, depth_dy) / max(abs(depth), 1e-4);
    const float depth_edge = smoothstep(0.015, 0.08, relative_depth_edge);
    // Appearance change and inconsistent optical flow describe responsive
    // shading (particles, reflections, foliage and transparency). Depth and
    // viewport failures instead describe geometric disocclusion. Keeping these
    // meanings separate lets NR and SR apply the intended temporal policy.
    responsivity = saturate(max(validation.x, validation.z));
    disocclusion = saturate(max(validation.y, max(outside, max(extreme_flow, depth_edge))));
}

float DLSS5_AIO_DilateResponsivity(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    const float2 pixel = 1.0 / float2(BUFFER_WIDTH, BUFFER_HEIGHT);
    float mask = tex2Dlod(sDLSS5_AIO_RawMask, float4(texcoord, 0.0, 0.0)).x;
    mask = max(mask, tex2Dlod(sDLSS5_AIO_RawMask, float4(texcoord + float2(pixel.x, 0.0), 0.0, 0.0)).x);
    mask = max(mask, tex2Dlod(sDLSS5_AIO_RawMask, float4(texcoord - float2(pixel.x, 0.0), 0.0, 0.0)).x);
    mask = max(mask, tex2Dlod(sDLSS5_AIO_RawMask, float4(texcoord + float2(0.0, pixel.y), 0.0, 0.0)).x);
    mask = max(mask, tex2Dlod(sDLSS5_AIO_RawMask, float4(texcoord - float2(0.0, pixel.y), 0.0, 0.0)).x);
    return mask;
}

float DLSS5_AIO_DilateDisocclusion(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    const float2 pixel = 1.0 / float2(BUFFER_WIDTH, BUFFER_HEIGHT);
    float mask = tex2Dlod(sDLSS5_AIO_RawDisocclusionMask, float4(texcoord, 0.0, 0.0)).x;
    mask = max(mask, tex2Dlod(sDLSS5_AIO_RawDisocclusionMask, float4(texcoord + float2(pixel.x, 0.0), 0.0, 0.0)).x);
    mask = max(mask, tex2Dlod(sDLSS5_AIO_RawDisocclusionMask, float4(texcoord - float2(pixel.x, 0.0), 0.0, 0.0)).x);
    mask = max(mask, tex2Dlod(sDLSS5_AIO_RawDisocclusionMask, float4(texcoord + float2(0.0, pixel.y), 0.0, 0.0)).x);
    mask = max(mask, tex2Dlod(sDLSS5_AIO_RawDisocclusionMask, float4(texcoord - float2(0.0, pixel.y), 0.0, 0.0)).x);
    return mask;
}

void DLSS5_AIO_StoreHistory(float4 position : SV_Position, float2 texcoord : TEXCOORD,
    out float luma : SV_Target0, out float depth : SV_Target1, out float2 motion : SV_Target2)
{
    luma = DLSS5_AIO_Luma(texcoord);
    depth = ReShade::GetLinearizedDepth(texcoord);
    // Store raw VORT flow so a rejection cannot poison the next comparison.
    motion = tex2Dlod(sMotVectTexVort, float4(texcoord, 0.0, 0.0)).xy;
}

technique DLSS5_AIO_Feed
<
    ui_label = "DLSS5 ReShade AIO guides (same-frame VORT motion + adaptive history rejection)";
    ui_tooltip = "Rendered manually at Present after VORT, before the DLSS5 ReShade AIO pipeline.";
>
{
    pass CaptureGuides
    {
        VertexShader = DLSS5_AIO_FullscreenVS;
        PixelShader = DLSS5_AIO_CaptureGuides;
        RenderTarget0 = DLSS5_AIO_MV;
        RenderTarget1 = DLSS5_AIO_Depth;
        RenderTarget2 = DLSS5_AIO_RawMask;
        RenderTarget3 = DLSS5_AIO_RawDisocclusionMask;
    }
    pass DilateResponsivity
    {
        VertexShader = DLSS5_AIO_FullscreenVS;
        PixelShader = DLSS5_AIO_DilateResponsivity;
        RenderTarget = DLSS5_AIO_Mask;
    }
    pass DilateDisocclusion
    {
        VertexShader = DLSS5_AIO_FullscreenVS;
        PixelShader = DLSS5_AIO_DilateDisocclusion;
        RenderTarget = DLSS5_AIO_DisocclusionMask;
    }
    pass StoreHistory
    {
        VertexShader = DLSS5_AIO_FullscreenVS;
        PixelShader = DLSS5_AIO_StoreHistory;
        RenderTarget0 = DLSS5_AIO_PrevLuma;
        RenderTarget1 = DLSS5_AIO_PrevDepth;
        RenderTarget2 = DLSS5_AIO_PrevMV;
    }
}

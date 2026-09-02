/*
    Disabled no-op technique used to keep ReShade's effects boundary available
    in Vulkan games that have no user shader collection installed.

    The standalone addon performs its Vulkan capture from the
    reshade_finish_effects callback. ReShade only enters that callback path when
    at least one technique was compiled, even if every technique is disabled.
*/

void StandaloneBoundaryVS(uint id : SV_VertexID, out float4 position : SV_Position)
{
    float2 uv = float2((id << 1) & 2, id & 2);
    position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 StandaloneBoundaryPS() : SV_Target
{
    return 0.0;
}

technique StandaloneBoundary
<
    ui_label = "Standalone DLSS-NR Vulkan boundary (leave disabled)";
    ui_tooltip = "A disabled no-op technique that exposes ReShade's effects boundary to the standalone Vulkan transport.";
>
{
    pass
    {
        VertexShader = StandaloneBoundaryVS;
        PixelShader = StandaloneBoundaryPS;
        BlendEnable = true;
        SrcBlend = ZERO;
        DestBlend = ONE;
    }
}

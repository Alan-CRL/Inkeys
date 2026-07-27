// imgui_vs.hlsl

#include "imgui_common.hlsli"

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;
    // 与 ImGui DX11 backend 的常量缓冲和输入布局保持一致。
    output.pos = mul(ProjectionMatrix, float4(input.pos.xy, 0.0f, 1.0f));
    output.col = input.col;
    output.uv = input.uv;
    return output;
}

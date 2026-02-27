// imgui_vs.hlsl

#include "imgui_common.hlsli"

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;
    // D3D11.1 SM 5.0 高效矩阵运算
    output.pos = mul(ProjectionMatrix, float4(input.pos.xy, 0.0f, 1.0f));
    output.col = input.col;
    output.uv = input.uv;
    return output;
}
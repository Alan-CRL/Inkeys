// imgui_ps.hlsl

#include "imgui_common.hlsli"

SamplerState sampler0 : register(s0);
Texture2D texture0 : register(t0);

float4 main(PS_INPUT input) : SV_Target
{
    // 1. 采样纹理并混合顶点颜色 (RGBA 逻辑)
    float4 out_col = input.col * texture0.Sample(sampler0, input.uv);
    
    // 2. 适配 B8G8R8A8 渲染目标
    // 将 R(x) 和 B(z) 进行交换。
    // .zyxw 意味着：输出R=采样B, 输出G=采样G, 输出B=采样R, 输出A=采样A
    return out_col.zyxw;
}
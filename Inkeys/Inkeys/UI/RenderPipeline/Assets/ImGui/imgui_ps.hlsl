// imgui_ps.hlsl

#include "imgui_common.hlsli"

SamplerState sampler0 : register(s0);
Texture2D texture0 : register(t0);

float4 main(PS_INPUT input) : SV_Target
{
    // BGRA texture format 已在采样阶段完成通道解释，无需再次交换 R/B。
    return input.col * texture0.Sample(sampler0, input.uv);
}

// imgui_ps.hlsl

#include "imgui_common.hlsli"

SamplerState sampler0 : register(s0);
Texture2D texture0 : register(t0);

#include "imgui_eraser_demo.hlsli"

float4 main(PS_INPUT input) : SV_Target
{
    // 整次 draw 使用同一模式；普通控件和主页字形保持原有采样路径。
    [branch] if (material.z > 0.5) return RenderEraserDemo(input.uv);
    // BGRA texture format 已在采样阶段完成通道解释，无需再次交换 R/B。
    return input.col * texture0.Sample(sampler0, input.uv);
}

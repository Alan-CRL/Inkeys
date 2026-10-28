// 橡皮演示独占像素着色器；顶点输入仍复用 ImGui VS 合同。

#include "imgui_common.hlsli"

SamplerState sampler0 : register(s0);
Texture2D texture0 : register(t0);

#include "imgui_eraser_demo.hlsli"

float4 main(PS_INPUT input) : SV_Target
{
    return RenderEraserDemo(input.uv);
}

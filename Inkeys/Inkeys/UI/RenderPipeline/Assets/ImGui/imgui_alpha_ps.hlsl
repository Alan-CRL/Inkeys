#include "imgui_common.hlsli"
SamplerState sampler0 : register(s0);
Texture2D texture0 : register(t0);
float4 main(PS_INPUT input) : SV_Target
{
    // R8只存覆盖率；等价于原RGBA图集的白色RGB与alpha，不改变混合/颜色空间。
    return input.col * float4(1.0f, 1.0f, 1.0f, texture0.Sample(sampler0, input.uv).r);
}

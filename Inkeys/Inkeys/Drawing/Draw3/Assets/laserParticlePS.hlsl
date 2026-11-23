// laserParticlePS.hlsl
// 画布粒子专用像素着色器：不带光晕的实心圆点，没有纹理采样、分支和常量缓冲区。

struct ParticleVertex
{
    float4 position : SV_POSITION;
    float2 offset : TEXCOORD0;
    nointerpolation float4 color : COLOR0;
    nointerpolation float radius : TEXCOORD1;
};

// 与墨迹像素着色器相同的双输出：add 为预乘颜色，retain 为保留下层的比例。
struct OperatorOutput
{
    float4 add : SV_Target0;
    float4 retain : SV_Target1;
};

OperatorOutput main(ParticleVertex input)
{
    // 边缘在 ±0.625 像素内平滑过渡，与激光主体的抗锯齿宽度一致。
    float edge = saturate((length(input.offset) - input.radius) * 0.8 + 0.5);
    float alpha = input.color.a * (1.0 - edge * edge * (3.0 - 2.0 * edge));
    OperatorOutput output;
    output.add = float4(input.color.rgb * alpha, alpha);
    output.retain = (1.0 - alpha).xxxx;
    return output;
}

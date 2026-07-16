// inkPixelShader.hlsl
#include "ink.hlsli"

float sdUnevenCapsule_Vertical(float2 p, float r1, float r2, float h)
{
    p.x = abs(p.x);
    if (abs(r1 - r2) >= h)
    {
        // 一个端点圆完全包含另一个时，退化为实际较大端点圆。
        return r1 >= r2 ? length(p) - r1 : length(p - float2(0.0, h)) - r2;
    }
    float b = (r1 - r2) / h;
    float a = sqrt(max(0.0, 1.0 - b * b));
    float k = dot(p, float2(-b, a));
    if (k < 0.0)
        return length(p) - r1;
    if (k > a * h)
        return length(p - float2(0.0, h)) - r2;
    return dot(p, float2(a, b)) - r1;
}

float GetInkDist_Convex(float2 p, float2 p1, float2 p2, float r1, float r2)
{
    float2 pa = p - p1;
    float2 ba = p2 - p1;
    float h = length(ba);
    if (h < 1e-5)
        return r1 >= r2 ? length(p - p1) - r1 : length(p - p2) - r2;
    float2 yAxis = ba / h;
    float2 xAxis = float2(-yAxis.y, yAxis.x);
    float2 p_local = float2(dot(pa, xAxis), dot(pa, yAxis));
    return sdUnevenCapsule_Vertical(p_local, r1, r2, h);
}

float4 main(PS_INPUT input) : SV_Target
{
    int type = (int) (input.shapeType + 0.5);

    if (any(isnan(input.p1)) || any(isnan(input.p2)))
        discard;

    if (type == 1)
    {
        return AlphaBlendSource.Sample(AlphaBlendSampler, input.uv);
    }

    if (type == 2)
    {
        float stableAlpha = AlphaBlendSource.Sample(AlphaBlendSampler, input.uv).a;
        float liveAlpha = AuxiliaryBlendSource.Sample(AlphaBlendSampler, input.uv).a;
        float maskAlpha = max(stableAlpha, liveAlpha); // 整笔覆盖率只取最大值，避免重叠段重复擦除。
        return float4(0.0, 0.0, 0.0, maskAlpha);
    }

    float d = 0.0;
    
    if (type == 0)
    {
        d = GetInkDist_Convex(input.pixPos, input.p1, input.p2, input.r1, input.r2);
    }

    //float aaWidth = fwidth(d);
    //aaWidth = max(aaWidth, 1e-5);
    //float alpha = saturate(0.5 - d / aaWidth);
    
    // 1. 获取屏幕空间的导数（基础像素宽度）
    float baseAaWidth = fwidth(d);
    
    // 2. 调节柔和度系数 (Softness Factor)
    // 1.0 = 标准锐利
    // 1.5 = 平滑且清晰 (推荐用于 Ink 风格)
    // 2.0+ = 开始变糊
    float softness = 1.5;
    
    float aaWidth = max(baseAaWidth * softness, 1e-5);
    
    // 3. 使用 smoothstep 进行 S 曲线过渡
    // 这里的逻辑是：
    // 当 d < -aaWidth/2 (形状内部) -> smoothstep 输出 0 -> alpha 为 1
    // 当 d >  aaWidth/2 (形状外部) -> smoothstep 输出 1 -> alpha 为 0
    // 中间区域平滑插值
    float alpha = 1.0 - smoothstep(-aaWidth * 0.5, aaWidth * 0.5, d);
    
    if (alpha <= 0.0)
        discard;

    float outAlpha = input.color.a * alpha;
    return float4(input.color.rgb * outAlpha, outAlpha);
}

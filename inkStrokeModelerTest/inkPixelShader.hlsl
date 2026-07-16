// inkPixelShader.hlsl
#include "ink.hlsli"

float sdUnevenCapsule_Vertical(float2 p, float r1, float r2, float h)
{
    p.x = abs(p.x);
    float distanceToInk = 0.0;
    if (abs(r1 - r2) >= h)
    {
        // 一个端点圆完全包含另一个时，退化为实际较大端点圆。
        float useSecondCircle = step(r1, r2);
        distanceToInk = length(p - float2(0.0, h * useSecondCircle)) - max(r1, r2);
    }
    else
    {
        float b = (r1 - r2) / h;
        float a = sqrt(max(0.0, 1.0 - b * b));
        float k = dot(p, float2(-b, a));
        if (k < 0.0)
            distanceToInk = length(p) - r1;
        else if (k > a * h)
            distanceToInk = length(p - float2(0.0, h)) - r2;
        else
            distanceToInk = dot(p, float2(a, b)) - r1;
    }
    return distanceToInk;
}

float GetInkDist_Convex(float2 p, float2 p1, float2 p2, float r1, float r2)
{
    float2 pa = p - p1;
    float2 ba = p2 - p1;
    float h = length(ba);
    float distanceToInk = 0.0;
    if (h < 1e-5)
    {
        float useSecondCircle = step(r1, r2);
        distanceToInk = length(p - lerp(p1, p2, useSecondCircle)) - max(r1, r2);
    }
    else
    {
        float2 yAxis = ba / h;
        float2 xAxis = float2(-yAxis.y, yAxis.x);
        float2 p_local = float2(dot(pa, xAxis), dot(pa, yAxis));
        distanceToInk = sdUnevenCapsule_Vertical(p_local, r1, r2, h);
    }
    return distanceToInk;
}

float Cross2D(float2 a, float2 b)
{
    return a.x * b.y - a.y * b.x;
}

float GetSharpStripDist(float2 p, float2 q0, float2 q1, float2 q2, float2 q3)
{
    // 顶点着色器保证四边形为凸；统一绕序后取四条边的最大有符号距离。
    float orientation = Cross2D(q1 - q0, q2 - q1) >= 0.0 ? 1.0 : -1.0;
    float d0 = -orientation * Cross2D(q1 - q0, p - q0) / max(length(q1 - q0), 1e-5);
    float d1 = -orientation * Cross2D(q2 - q1, p - q1) / max(length(q2 - q1), 1e-5);
    float d2 = -orientation * Cross2D(q3 - q2, p - q2) / max(length(q3 - q2), 1e-5);
    float d3 = -orientation * Cross2D(q0 - q3, p - q3) / max(length(q0 - q3), 1e-5);
    return max(max(d0, d1), max(d2, d3));
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

    if (type == 4)
    {
        float stableAlpha = AlphaBlendSource.Sample(AlphaBlendSampler, input.uv).a;
        float liveAlpha = AuxiliaryBlendSource.Sample(AlphaBlendSampler, input.uv).a;
        float coverage = max(stableAlpha, liveAlpha);
        float outAlpha = input.color.a * coverage;
        if (outAlpha <= 0.0)
            discard;
        return float4(input.color.rgb * outAlpha, outAlpha); // 整笔覆盖率着色后只做一次 source-over。
    }

    float d = 0.0;
    
    if (type == 0)
    {
        d = GetInkDist_Convex(input.pixPos, input.p1, input.p2, input.r1, input.r2);
    }
    else if (type == 3)
    {
        d = GetSharpStripDist(input.pixPos, input.p1, input.p2, input.direction1, input.direction2);
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

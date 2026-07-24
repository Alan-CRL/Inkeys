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

float GetFixedNibSweepDist(float2 p, float2 p1, float2 p2, float2 halfSize)
{
    float2 segment = p2 - p1;
    if (dot(segment, segment) <= 1e-10)
    {
        float2 q = abs(p - p1) - halfSize;
        return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
    }

    // sweep 是三个方向半平面的交集：X/Y 轴向边界和线段法线边界。
    float2 centerMin = min(p1, p2);
    float2 centerMax = max(p1, p2);
    float2 axisDistance = max(centerMin - halfSize - p, p - centerMax - halfSize);
    float distanceToSweep = max(axisDistance.x, axisDistance.y);
    float2 normal = normalize(float2(-segment.y, segment.x));
    float normalSupport = dot(abs(normal), halfSize);
    distanceToSweep = max(distanceToSweep,
        abs(dot(p - p1, normal)) - normalSupport);
    return distanceToSweep;
}

struct OperatorOutput
{
    float4 add : SV_Target0;
    float4 retain : SV_Target1;
};

OperatorOutput main(PS_INPUT input)
{
    int type = (int) (input.shapeType + 0.5);

    if (any(isnan(input.p1)) || any(isnan(input.p2)))
        discard;

    if (type == 1 || type == 2)
    {
        float4 stableAdd = StableOperatorAdd.Sample(OperatorSampler, input.uv);
        float stableRetain = StableOperatorRetain.Sample(OperatorSampler, input.uv).r;
        float4 liveAdd = LiveOperatorAdd.Sample(OperatorSampler, input.uv);
        float liveRetain = LiveOperatorRetain.Sample(OperatorSampler, input.uv).r;
        OperatorOutput output;
        if (type == 1)
        {
            // L1/L0 是同一笔的两段，先取覆盖率并集，避免连接处重复抗锯齿。
            output.add = max(stableAdd, liveAdd);
            output.retain = min(stableRetain, liveRetain).xxxx;
        }
        else
        {
            // 有序操作：New(实时层) after Old(稳定层)。
            output.add = liveAdd + liveRetain * stableAdd;
            output.retain = (liveRetain * stableRetain).xxxx;
        }
        return output;
    }

    float d = 0.0;
    
    if (type == 0)
    {
        d = GetInkDist_Convex(input.pixPos, input.p1, input.p2, input.r1, input.r2);
    }
    else if (type == 3)
        d = GetFixedNibSweepDist(input.pixPos, input.p1, input.p2,
            float2(input.r1, input.r2));

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

    OperatorOutput output;
    if (globalOperatorKind == 1)
    {
        output.add = 0.0;
        output.retain = (1.0 - alpha).xxxx; // 橡皮只保留下层的一部分，不产生颜色。
    }
    else
    {
        float outAlpha = input.color.a * alpha;
        if (outAlpha <= 0.0)
            discard;
        output.add = float4(input.color.rgb * outAlpha, outAlpha);
        output.retain = (1.0 - outAlpha).xxxx;
    }
    return output;
}

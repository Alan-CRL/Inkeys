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

float GetFlatBodyDist(float2 p, float2 p1, float2 p2, float radius)
{
    float2 segment = p2 - p1;
    float rawSegmentLength = length(segment);
    float segmentLength = max(rawSegmentLength, 1e-5);
    float2 tangent = rawSegmentLength > 1e-5 ? segment / rawSegmentLength : float2(1.0, 0.0);
    float2 normal = float2(-tangent.y, tangent.x);
    float2 center = (p1 + p2) * 0.5;
    float2 local = float2(dot(p - center, tangent), dot(p - center, normal));
    float2 q = abs(local) - float2(segmentLength * 0.5, radius);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}

float IsInsideRoundJoinSector(float2 vectorFromCenter, float2 startDirection,
    float2 endDirection, float orientation)
{
    float inside = 1.0;
    if (dot(vectorFromCenter, vectorFromCenter) > 1e-10)
    {
        float signValue = orientation >= 0.0 ? 1.0 : -1.0;
        // 径向边使用硬裁切，邻接 body 的 2px 重叠负责覆盖边界，不产生第二条 AA 接缝。
        inside = Cross2D(startDirection, vectorFromCenter) * signValue >= -1e-5 &&
            Cross2D(vectorFromCenter, endDirection) * signValue >= -1e-5 ? 1.0 : 0.0;
    }
    return inside;
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
        if (input.primitiveType == 0 || input.primitiveType == 3)
        {
            d = GetFlatBodyDist(input.pixPos, input.p1, input.p2, input.r1);
        }
        else if (input.primitiveType == 1)
        {
            float2 vectorFromCenter = input.pixPos - input.p1;
            if (IsInsideRoundJoinSector(vectorFromCenter, input.direction1, input.direction2, input.r2) < 0.5)
                discard;
            d = length(vectorFromCenter) - input.r1;
        }
        else if (input.primitiveType == 2)
        {
            d = length(input.pixPos - input.p1) - input.r1;
        }
        else
        {
            discard;
        }
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

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

float GetRectangleDist(float2 p, float2 halfSize)
{
    float2 q = abs(p) - halfSize;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}

float GetVerticalCapsuleDist(float2 p, float radius, float halfHeight)
{
    float segmentHalfHeight = max(halfHeight - radius, 0.0);
    p.y -= clamp(p.y, -segmentHalfHeight, segmentHalfHeight);
    return length(p) - radius;
}

float CursorCoverage(float distanceToShape)
{
    // 与旧 CPU 栅格器一致，零等值线两侧共使用 1px 线性 AA。
    return saturate(0.5 - distanceToShape);
}

struct OperatorOutput
{
    float4 add : SV_Target0;
    float4 retain : SV_Target1;
};

float LaserAaCoverage(float distanceToEdge)
{
    float aaWidth = max(fwidth(distanceToEdge) * 1.25, 1e-4);
    return 1.0 - smoothstep(-aaWidth * 0.5, aaWidth * 0.5, distanceToEdge);
}

float LaserDiffuseCoverage(float distanceFromSolid)
{
    // 实体边界为 1，固定漫反射外缘为 0；平方曲线更快收束，仍只需一次乘法。
    float fade = saturate(1.0 - distanceFromSolid / max(laserRadii.z, 1e-4));
    return fade * fade;
}

float4 GetLaserDotCoverage(float distanceToCenter, float solidRadius)
{
    float baseSolidRadius = max(laserRadii.y, 1e-4);
    float coreRadius = solidRadius * laserRadii.x / baseSolidRadius;
    float scatterWidth = solidRadius * laserRadii.w / baseSolidRadius;
    float borderDistance = distanceToCenter - solidRadius;
    float core = LaserAaCoverage(distanceToCenter - coreRadius);
    float scatter = LaserAaCoverage(abs(distanceToCenter - coreRadius) - scatterWidth);
    float border = LaserAaCoverage(borderDistance);
    float diffuse = LaserDiffuseCoverage(borderDistance);
    return saturate(float4(core, scatter, border, diffuse));
}

float4 GetLaserStrokeCoverage(PS_INPUT input)
{
    float baseSolidRadius = max(laserRadii.y, 1e-4);
    float coreRatio = laserRadii.x / baseSolidRadius;
    float coreDistance = GetInkDist_Convex(
        input.pixPos, input.p1, input.p2, input.r1 * coreRatio, input.r2 * coreRatio);
    float borderDistance = GetInkDist_Convex(
        input.pixPos, input.p1, input.p2, input.r1, input.r2);

    float2 segment = input.p2 - input.p1;
    float segmentLengthSquared = dot(segment, segment);
    float segmentRatio = segmentLengthSquared > 1e-8
        ? saturate(dot(input.pixPos - input.p1, segment) / segmentLengthSquared) : 0.0;
    float localSolidRadius = max(lerp(input.r1, input.r2, segmentRatio), 1e-4);
    float scatterWidth = laserRadii.w * localSolidRadius / baseSolidRadius;

    float core = LaserAaCoverage(coreDistance);
    float scatter = LaserAaCoverage(abs(coreDistance) - scatterWidth);
    float border = LaserAaCoverage(borderDistance);
    float diffuse = LaserDiffuseCoverage(borderDistance);
    return saturate(float4(core, scatter, border, diffuse));
}

float4 LayerPremultiplied(float4 below, float4 color, float coverage)
{
    float alpha = saturate(color.a * coverage);
    return float4(color.rgb * alpha, alpha) + (1.0 - alpha) * below;
}

OperatorOutput ResolveLaserMaterial(float4 coverage, float opacity)
{
    float4 color = 0.0;
    float edgeMix = smoothstep(laserParameters.z, laserParameters.w, coverage.a) *
        (1.0 - coverage.b) * laserEdgeColor.a;
    float4 diffuseColor = float4(
        lerp(laserGlowColor.rgb, laserEdgeColor.rgb, edgeMix), laserGlowColor.a);
    color = LayerPremultiplied(color, diffuseColor, coverage.a);
    color = LayerPremultiplied(color, laserBorderColor, coverage.b);
    color = LayerPremultiplied(color, laserScatterColor, coverage.g);
    color = LayerPremultiplied(color, laserCoreColor, coverage.r);
    color *= saturate(opacity);
    OperatorOutput output;
    output.add = color;
    output.retain = (1.0 - color.a).xxxx;
    return output;
}

OperatorOutput main(PS_INPUT input)
{
    int type = (int) (input.shapeType + 0.5);

    if (any(isnan(input.p1)) || any(isnan(input.p2)))
        discard;

    if (type == 9 || type == 10)
    {
        float distanceToCenter = length(input.pixPos - input.p1);
        if (type == 9)
            return ResolveLaserMaterial(GetLaserDotCoverage(
                distanceToCenter, input.r1 > 0.0 ? input.r1 : laserRadii.y),
                input.r2 * laserParameters.x);

        float coreCoverage = LaserAaCoverage(distanceToCenter - input.r1);
        float glowExtent = max(input.p2.x, 1e-4);
        float glowRadius = input.r1 + glowExtent;
        float glowDistance = max(distanceToCenter - input.r1, 0.0);
        float glowCoverage = LaserAaCoverage(distanceToCenter - glowRadius) *
            pow(saturate(1.0 - glowDistance / glowExtent), 1.6);
        float opacity = saturate(input.r2);
        float brightness = saturate(input.color.y);
        float3 glowColor = float3(
            globalPadding.x, input.color.z, input.color.w) * brightness;
        // coreColorWhiteMix (globalPadding.z) 作为 lerp 上限而非下限：
        // 亮大粒子最多混合到 coreColorWhiteMix% 的白色，始终保留 (1-coreColorWhiteMix) 的红色调；
        // 暗小粒子接近纯红粉色。效果：全体粒子"白中带红"而非纯白。
        float coreColorMix = globalPadding.z * saturate(input.p2.y);
        float3 coreColor = lerp(
            float3(globalPadding.x, input.color.z, input.color.w),
            laserScatterColor.rgb, coreColorMix) * brightness;
        float4 particle = 0.0;
        particle = LayerPremultiplied(particle,
            float4(glowColor, globalPadding.y), glowCoverage);
        particle = LayerPremultiplied(particle,
            float4(coreColor, 0.92), coreCoverage);
        particle *= opacity;
        OperatorOutput particleOutput;
        particleOutput.add = particle;
        particleOutput.retain = (1.0 - particle.a).xxxx;
        return particleOutput;
    }

    if (type >= 4 && type <= 6)
    {
        float2 local = input.pixPos - input.p1;
        float distanceToShape = type == 5
            ? GetRectangleDist(local, input.p2)
            : length(local) - min(input.p2.x, input.p2.y);
        float outerCoverage = CursorCoverage(distanceToShape);
        float innerCoverage = CursorCoverage(distanceToShape + max(input.r1, 0.0));
        float outlineCoverage = max(0.0, outerCoverage - innerCoverage);
        float opacity = saturate(input.color.a);
        float outlineAlpha = outlineCoverage * opacity;
        float fillAlpha = innerCoverage * saturate(input.r2) * opacity;
        float alpha = outlineAlpha + fillAlpha;
        float3 outlineColor = saturate(globalPadding);
        float3 rgb = 0.0;
        if (alpha > 0.0)
            rgb = (outlineAlpha * outlineColor + fillAlpha * saturate(input.color.rgb)) / alpha;

        if (type == 6)
        {
            float diameter = min(input.p2.x, input.p2.y) * 2.0;
            float stripeRadius = diameter * 0.05;
            float stripeHalfHeight = diameter * 0.24;
            float stripeOffset = diameter * 0.12;
            float stripeCoverage = max(
                CursorCoverage(GetVerticalCapsuleDist(
                    local - float2(-stripeOffset, 0.0), stripeRadius, stripeHalfHeight)),
                CursorCoverage(GetVerticalCapsuleDist(
                    local - float2(stripeOffset, 0.0), stripeRadius, stripeHalfHeight)));
            stripeCoverage = min(stripeCoverage, innerCoverage);
            rgb += stripeCoverage * (outlineColor - rgb);
            alpha += stripeCoverage * (opacity - alpha);
        }

        if (alpha <= 0.0)
            discard;
        OperatorOutput cursorOutput;
        cursorOutput.add = float4(rgb * alpha, alpha);
        cursorOutput.retain = (1.0 - alpha).xxxx;
        return cursorOutput;
    }

    if (type == 8)
    {
        float4 coverage = LaserStrokeCoverage.Sample(OperatorSampler, input.uv);
        return ResolveLaserMaterial(coverage, laserParameters.x);
    }

    if (type == 11)
    {
        float4 color = LaserCompositedColor.Sample(OperatorSampler, input.uv) *
            saturate(laserParameters.x);
        OperatorOutput stableOutput;
        stableOutput.add = color;
        stableOutput.retain = (1.0 - color.a).xxxx;
        return stableOutput;
    }

    if (type == 12)
    {
        // scratch 局部清理时关闭混合，矩形内直接覆盖为零。
        OperatorOutput clearOutput;
        clearOutput.add = 0.0;
        clearOutput.retain = 0.0;
        return clearOutput;
    }

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

    if (type == 7)
    {
        OperatorOutput coverageOutput;
        coverageOutput.add = GetLaserStrokeCoverage(input);
        coverageOutput.retain = 0.0;
        return coverageOutput;
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

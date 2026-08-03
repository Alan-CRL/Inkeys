// inkVertexShader.hlsl
#include "ink.hlsli"

static const float2 kQuadUVs[6] =
{
    float2(0, 0), float2(1, 0), float2(0, 1),
    float2(0, 1), float2(1, 0), float2(1, 1)
};

float2 SafeNormalize(float2 value, float2 fallbackValue)
{
    float valueLength = length(value);
    return valueLength > 1e-5 ? value / valueLength : fallbackValue;
}

PS_INPUT main(uint id : SV_VertexID, uint instanceId : SV_InstanceID)
{
    PS_INPUT output = (PS_INPUT) 0;
    int type = (int) (globalShapeType + 0.5);
    uint itemIndex = id / 6;
    uint vertexIndex = id % 6;
    float2 templatePos = kQuadUVs[vertexIndex];

    output.color = globalColor;
    output.shapeType = globalShapeType;

    if (type == 10)
    {
        LaserGpuParticle particle = LaserParticleData[instanceId];
        if (particle.alive == 0 || particle.opacity <= 0.0 ||
            particle.currentRadius <= 0.0)
        {
            // 固定实例绘制中，死亡槽生成退化图元，不需要 GPU 计数器回读。
            output.pos = float4(-2.0, -2.0, 0.0, 1.0);
            output.p1 = 0.0;
            output.p2 = 0.0;
            return output;
        }

        // 比例辉光叠加固定 2 DIP 地板，确保最小粒子在任意 DPI 下均有肉眼可见的辉光。
        float glowExtent = max(particle.currentRadius *
            max(globalColor.x, 0.0) + 2.0 * laserParameters.y, 1e-4);
        float outerRadius = particle.currentRadius + glowExtent;
        float2 center = particle.position;
        float2 rectMin = center - outerRadius - 2.0;
        float2 rectMax = center + outerRadius + 2.0;
        float2 worldPos = lerp(rectMin, rectMax, templatePos);
        output.pos = float4((worldPos.x / screenWidth) * 2.0 - 1.0,
            -((worldPos.y / screenHeight) * 2.0 - 1.0), 0.0, 1.0);
        output.pixPos = worldPos;
        output.p1 = center;
        output.p2 = float2(glowExtent, particle.baseBrightness);
        output.r1 = particle.currentRadius;
        output.r2 = particle.opacity;
        output.color.y = particle.currentBrightness;
        return output;
    }

    if (type == 9)
    {
        InkPoint dot = InkData[globalBufferOffset + itemIndex];
        float outerRadius = max(dot.r, 0.0) + laserRadii.z;
        float2 center = dot.pos;
        float2 rectMin = center - outerRadius - 2.0;
        float2 rectMax = center + outerRadius + 2.0;
        float2 worldPos = lerp(rectMin, rectMax, templatePos);
        output.pos = float4((worldPos.x / screenWidth) * 2.0 - 1.0,
            -((worldPos.y / screenHeight) * 2.0 - 1.0), 0.0, 1.0);
        output.pixPos = worldPos;
        output.p1 = center;
        output.p2 = outerRadius.xx;
        output.r1 = dot.r;
        output.r2 = dot.time;
        return output;
    }

    if (type >= 4 && type <= 6)
    {
        InkPoint cursor = InkData[globalBufferOffset];
        InkPoint style = InkData[globalBufferOffset + 1];
        float2 center = cursor.pos;
        float2 halfSize = float2(cursor.r, cursor.time);
        float2 rectMin = center - halfSize - 2.0;
        float2 rectMax = center + halfSize + 2.0;
        float2 worldPos = lerp(rectMin, rectMax, templatePos);
        output.pos = float4((worldPos.x / screenWidth) * 2.0 - 1.0,
            -((worldPos.y / screenHeight) * 2.0 - 1.0), 0.0, 1.0);
        output.pixPos = worldPos;
        output.p1 = center;
        output.p2 = halfSize;
        output.r1 = style.pos.x;
        output.r2 = style.pos.y;
        return output;
    }

    if (type == 3)
    {
        HighlighterPrimitive primitive = HighlighterData[globalBufferOffset + itemIndex];
        // 固定竖直矩形沿 p1→p2 扫掠，AABB 覆盖 sweep 的轴向边界。
        float2 rectMin = min(primitive.p1, primitive.p2) - primitive.halfSize - 2.0;
        float2 rectMax = max(primitive.p1, primitive.p2) + primitive.halfSize + 2.0;
        output.p1 = primitive.p1;
        output.p2 = primitive.p2;
        output.r1 = primitive.halfSize.x;
        output.r2 = primitive.halfSize.y;

        float2 worldPos = lerp(rectMin, rectMax, templatePos);
        output.pos = float4((worldPos.x / screenWidth) * 2.0 - 1.0,
            -((worldPos.y / screenHeight) * 2.0 - 1.0), 0.0, 1.0);
        output.pixPos = worldPos;
        return output;
    }

    if (type == 8 || type == 11 || type == 12 || type == 13)
    {
        // Laser 矩形 pass 直接使用 b0，避免为两个角点上传和绑定 t0。
        float2 rectMin = globalColor.xy;
        float2 rectMax = globalColor.zw;
        float2 worldPos = lerp(rectMin, rectMax, templatePos);
        output.pos = float4((worldPos.x / screenWidth) * 2.0 - 1.0,
            -((worldPos.y / screenHeight) * 2.0 - 1.0), 0.0, 1.0);
        output.pixPos = worldPos;
        output.uv = worldPos / float2(screenWidth, screenHeight);
        output.p1 = rectMin;
        output.p2 = rectMax;
        return output;
    }

    uint realIndex = globalBufferOffset + itemIndex;
    InkPoint data1 = InkData[realIndex];
    InkPoint data2 = InkData[realIndex + 1];
    float2 p1 = data1.pos;
    float2 p2 = data2.pos;
    float r1 = data1.r;
    float r2 = data2.r;

    if (type == 1 || type == 2)
    {
        float2 rectMin = min(p1, p2);
        float2 rectMax = max(p1, p2);
        float2 worldPos = lerp(rectMin, rectMax, templatePos);
        output.pos = float4((worldPos.x / screenWidth) * 2.0 - 1.0,
            -((worldPos.y / screenHeight) * 2.0 - 1.0), 0.0, 1.0);
        output.pixPos = worldPos;
        output.uv = worldPos / float2(screenWidth, screenHeight);
        output.p1 = rectMin;
        output.p2 = rectMax;
        return output;
    }

    // 普通笔、橡皮和 Laser 都使用端点半径构造足够覆盖完整材质的 OBB。
    float2 segment = p2 - p1;
    float segmentLength = length(segment);
    float2 tangent = segmentLength > 0.001 ? segment / segmentLength : float2(1.0, 0.0);
    float2 normal = float2(-tangent.y, tangent.x);
    float startRadius = type == 7 ? max(r1, 0.0) + laserRadii.z : r1;
    float endRadius = type == 7 ? max(r2, 0.0) + laserRadii.z : r2;
    float maxRadius = max(startRadius, endRadius);
    float localX = lerp(-startRadius - 2.0, segmentLength + endRadius + 2.0, templatePos.x);
    float localY = lerp(-maxRadius - 2.0, maxRadius + 2.0, templatePos.y);
    float2 worldPos = p1 + tangent * localX + normal * localY;

    output.pos = float4((worldPos.x / screenWidth) * 2.0 - 1.0,
        -((worldPos.y / screenHeight) * 2.0 - 1.0), 0.0, 1.0);
    output.pixPos = worldPos;
    output.p1 = p1;
    output.p2 = p2;
    output.r1 = r1;
    output.r2 = r2;
    return output;
}

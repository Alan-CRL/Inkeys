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

PS_INPUT main(uint id : SV_VertexID)
{
    PS_INPUT output = (PS_INPUT) 0;
    int type = (int) (globalShapeType + 0.5);
    uint itemIndex = id / 6;
    uint vertexIndex = id % 6;
    float2 templatePos = kQuadUVs[vertexIndex];

    output.color = globalColor;
    output.shapeType = globalShapeType;

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

    // 普通笔和橡皮继续使用原有的变半径圆胶囊 OBB。
    float2 segment = p2 - p1;
    float segmentLength = length(segment);
    float2 tangent = segmentLength > 0.001 ? segment / segmentLength : float2(1.0, 0.0);
    float2 normal = float2(-tangent.y, tangent.x);
    float maxRadius = max(r1, r2);
    float localX = lerp(-r1 - 2.0, segmentLength + r2 + 2.0, templatePos.x);
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

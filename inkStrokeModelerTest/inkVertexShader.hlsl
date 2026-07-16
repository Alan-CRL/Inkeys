// inkVertexShader.hlsl
#include "ink.hlsli"

static const float2 kQuadUVs[6] =
{
    float2(0, 0), float2(1, 0), float2(0, 1),
    float2(0, 1), float2(1, 0), float2(1, 1)
};

float Cross2D(float2 a, float2 b)
{
    return a.x * b.y - a.y * b.x;
}

float2 SafeNormalize(float2 value, float2 fallbackValue)
{
    float valueLength = length(value);
    return valueLength > 1e-5 ? value / valueLength : fallbackValue;
}

bool IsConvexQuad(float2 q0, float2 q1, float2 q2, float2 q3)
{
    float c0 = Cross2D(q1 - q0, q2 - q1);
    float c1 = Cross2D(q2 - q1, q3 - q2);
    float c2 = Cross2D(q3 - q2, q0 - q3);
    float c3 = Cross2D(q0 - q3, q1 - q0);
    const float epsilon = 1e-4;
    return (c0 > epsilon && c1 > epsilon && c2 > epsilon && c3 > epsilon) ||
        (c0 < -epsilon && c1 < -epsilon && c2 < -epsilon && c3 < -epsilon);
}

PS_INPUT main(uint id : SV_VertexID)
{
    PS_INPUT output;
    int type = (int) (globalShapeType + 0.5);
    
    // 1. 计算相对索引
    uint segmentIndex = id / 6;
    uint vertexIndex = id % 6;
    
    // 2. 【关键】计算绝对索引：加上全局偏移量
    uint realIndex = globalBufferOffset + segmentIndex;
    
    // 3. Vertex Pulling
    InkPoint data1 = InkData[realIndex];
    InkPoint data2 = InkData[realIndex + 1];

    float2 p1 = data1.pos;
    float2 p2 = data2.pos;
    float r1 = data1.r;
    float r2 = data2.r;
    
    float2 templatePos = kQuadUVs[vertexIndex];

    if (type == 1 || type == 2 || type == 4)
    {
        float2 rectMin = min(p1, p2);
        float2 rectMax = max(p1, p2);
        float2 worldPos = lerp(rectMin, rectMax, templatePos);

        float x = (worldPos.x / screenWidth) * 2.0 - 1.0;
        float y = -((worldPos.y / screenHeight) * 2.0 - 1.0);

        output.pos = float4(x, y, 0.0, 1.0);
        output.pixPos = worldPos;
        output.uv = worldPos / float2(screenWidth, screenHeight);
        output.color = globalColor;
        output.shapeType = globalShapeType;
        output.p1 = rectMin;
        output.p2 = rectMax;
        output.r1 = 0.0;
        output.r2 = 0.0;
        output.direction1 = float2(1.0, 0.0);
        output.direction2 = float2(1.0, 0.0);

        return output;
    }

    if (type == 3)
    {
        // 平头荧光笔由相邻端点的共享横截面构成，避免分段接缝。
        float2 direction1 = SafeNormalize(data1.direction, float2(1.0, 0.0));
        float2 direction2 = SafeNormalize(data2.direction, direction1);
        float2 segment = p2 - p1;
        float segmentLength = length(segment);
        if (segmentLength <= 1e-5)
        {
            // 全程零位移时将退化段扩展为居中的方形印记。
            float halfSize = max(r1, r2);
            float2 center = p1;
            p1 = center - direction2 * halfSize;
            p2 = center + direction2 * halfSize;
            r1 = halfSize;
            r2 = halfSize;
            direction1 = direction2;
            segment = p2 - p1;
            segmentLength = length(segment);
        }

        float2 tangent = segmentLength > 1e-5 ? segment / segmentLength : float2(1.0, 0.0);
        if (dot(direction1, tangent) < 0.0) direction1 = -direction1;
        if (dot(direction2, tangent) < 0.0) direction2 = -direction2;

        float2 normal1 = float2(-direction1.y, direction1.x);
        float2 normal2 = float2(-direction2.y, direction2.x);
        float2 q0 = p1 + normal1 * r1;
        float2 q1 = p2 + normal2 * r2;
        float2 q2 = p2 - normal2 * r2;
        float2 q3 = p1 - normal1 * r1;
        if (!IsConvexQuad(q0, q1, q2, q3))
        {
            // 极端折返时回退到线段方向矩形，保证 AABB 和像素距离始终有效。
            normal1 = float2(-tangent.y, tangent.x);
            normal2 = normal1;
            q0 = p1 + normal1 * r1;
            q1 = p2 + normal2 * r2;
            q2 = p2 - normal2 * r2;
            q3 = p1 - normal1 * r1;
        }

        float2 rectMin = min(min(q0, q1), min(q2, q3)) - 2.0;
        float2 rectMax = max(max(q0, q1), max(q2, q3)) + 2.0;
        float2 worldPos = lerp(rectMin, rectMax, templatePos);
        float x = (worldPos.x / screenWidth) * 2.0 - 1.0;
        float y = -((worldPos.y / screenHeight) * 2.0 - 1.0);

        output.pos = float4(x, y, 0.0, 1.0);
        output.pixPos = worldPos;
        output.uv = float2(0.0, 0.0);
        output.color = globalColor;
        output.shapeType = globalShapeType;
        output.p1 = q0;
        output.p2 = q1;
        output.r1 = 0.0;
        output.r2 = 0.0;
        output.direction1 = q2;
        output.direction2 = q3;
        return output;
    }

    // --- OBB 计算 ---
    float2 dir = p2 - p1;
    float len = length(dir);
    
    float2 tangent = (len > 0.001) ? (dir / len) : float2(1.0, 0.0);
    float2 normal = float2(-tangent.y, tangent.x);
    
    float paddingVal = 2.0;
    float maxR = max(r1, r2);
    
    float localX = lerp(-r1 - paddingVal, len + r2 + paddingVal, templatePos.x);
    float localY = lerp(-maxR - paddingVal, maxR + paddingVal, templatePos.y);
    
    float2 worldPos = p1 + tangent * localX + normal * localY;
    
    // --- 坐标系转换 ---
    float x = (worldPos.x / screenWidth) * 2.0 - 1.0;
    float y = -((worldPos.y / screenHeight) * 2.0 - 1.0);
    
    output.pos = float4(x, y, 0.0, 1.0);
    output.pixPos = worldPos;
    output.uv = float2(0.0, 0.0);
    output.color = globalColor;
    output.shapeType = globalShapeType;
    output.p1 = p1;
    output.p2 = p2;
    output.r1 = r1;
    output.r2 = r2;
    output.direction1 = data1.direction;
    output.direction2 = data2.direction;
    
    return output;
}

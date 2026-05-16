// inkVertexShader.hlsl
#include "ink.hlsli"

static const float2 kQuadUVs[6] =
{
    float2(0, 0), float2(1, 0), float2(0, 1),
    float2(0, 1), float2(1, 0), float2(1, 1)
};

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

    if (type == 1)
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
    
    return output;
}
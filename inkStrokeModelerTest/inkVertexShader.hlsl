#include "ink.hlsli"

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;
    
    // --- OBB (Oriented Bounding Box) 计算 ---
    
    float2 dir = input.p2 - input.p1;
    float len = length(dir);
    
    // 防止两点重合导致除零
    float2 tangent = (len > 0.001) ? (dir / len) : float2(1.0, 0.0);
    // 法线：将切线旋转90度 (-y, x)
    float2 normal = float2(-tangent.y, tangent.x);
    
    // 扩展边距，防止SDF抗锯齿被切掉
    float paddingVal = 2.0;
    
    // 我们将 TemplatePos (0~1, 0~1) 映射到胶囊的局部坐标系
    // Local X (沿轴向): 从 -r1 到 len + r2
    // Local Y (垂直轴向): 从 -maxR 到 +maxR
    
    float maxR = max(input.r1, input.r2);
    
    // 计算局部坐标
    float localX = lerp(-input.r1 - paddingVal, len + input.r2 + paddingVal, input.templatePos.x);
    float localY = lerp(-maxR - paddingVal, maxR + paddingVal, input.templatePos.y);
    
    // 变换回世界坐标: P1 + Tangent*x + Normal*y
    float2 worldPos = input.p1 + tangent * localX + normal * localY;
    
    // --- 坐标系转换 (World -> NDC) ---
    float x = (worldPos.x / screenWidth) * 2.0 - 1.0;
    float y = -((worldPos.y / screenHeight) * 2.0 - 1.0); // Y轴翻转
    
    output.pos = float4(x, y, 0.0, 1.0);
    
    // --- 传递数据 ---
    output.pixPos = worldPos;
    output.color = input.color;
    output.p1 = input.p1;
    output.p2 = input.p2;
    output.r1 = input.r1;
    output.r2 = input.r2;
    output.shapeType = input.shapeType;
    
    return output;
}
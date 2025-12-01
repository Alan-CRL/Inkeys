// inkVertexShader.hlsl

#include "ink.hlsli"

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;
    
    // --- 1. 动态计算包围盒 (Bounding Box) ---
    // 根据实例数据 (P1, P2, R1, R2) 计算覆盖范围
    // 以前这步在 CPU 做，现在由 GPU 的每个实例做
    
    float paddingVal = 2.0; // 扩展边距，防止抗锯齿边缘被裁切
    
    float minX = min(input.p1.x - input.r1, input.p2.x - input.r2) - paddingVal;
    float minY = min(input.p1.y - input.r1, input.p2.y - input.r2) - paddingVal;
    float maxX = max(input.p1.x + input.r1, input.p2.x + input.r2) + paddingVal;
    float maxY = max(input.p1.y + input.r1, input.p2.y + input.r2) + paddingVal;
    
    float width = maxX - minX;
    float height = maxY - minY;
    
    // --- 2. 顶点位置插值 ---
    // input.templatePos 来自 Slot 0，是标准的单位矩形 (0,0) -> (1,1)
    // 我们将其“拉伸”并“平移”到包围盒的位置
    float2 worldPos;
    worldPos.x = minX + width * input.templatePos.x;
    worldPos.y = minY + height * input.templatePos.y;
    
    // --- 3. 坐标空间转换 ---
    // 屏幕空间 (Pixels) -> NDC空间 (-1 ~ 1)
    // 注意 Y 轴翻转：屏幕坐标 Y 向下，NDC Y 向上
    float x = (worldPos.x / screenWidth) * 2.0 - 1.0;
    float y = -((worldPos.y / screenHeight) * 2.0 - 1.0);
    
    output.pos = float4(x, y, 0.0, 1.0);
    
    // --- 4. 传递数据给 Pixel Shader ---
    output.pixPos = worldPos; // 用于 SDF 距离计算
    output.color = input.color;
    output.p1 = input.p1;
    output.p2 = input.p2;
    output.r1 = input.r1;
    output.r2 = input.r2;
    output.shapeType = input.shapeType;
    
    return output;
}
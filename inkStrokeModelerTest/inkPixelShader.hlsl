// inkPixelShader.hlsl

#include "ink.hlsli"

float sdUnevenCapsule_Vertical(float2 p, float r1, float r2, float h)
{
    // 由于是对称的，只计算 x 的绝对值
    p.x = abs(p.x);
    
    // b = sin(alpha)
    float b = (r1 - r2) / h;
    
    float b2 = b * b;
    if (b2 > 1.0)
    {
        // 退化情况：返回到较大圆的距离
        return length(p) - max(r1, r2);
    }
    
    float a = sqrt(max(0.0, 1.0 - b2)); // a = cos(alpha)
    
    // 关键点积判断区域
    float k = dot(p, float2(-b, a));
    
    if (k < 0.0) return length(p) - r1; // P1 圆帽区域
    if (k > a * h) return length(p - float2(0.0, h)) - r2; // P2 圆帽区域
    
    return dot(p, float2(a, b)) - r1; // 梯形/圆锥侧面区域
}

// 包装函数：处理坐标旋转和平移
float GetInkDist_Convex(float2 p, float2 p1, float2 p2, float r1, float r2)
{
    float2 pa = p - p1; // 将 P 平移到 P1 为原点
    float2 ba = p2 - p1; // 轴向量
    float h = length(ba); // 高度
    
    if (h < 0.1) return length(pa) - r1;
    
    // 归一化轴向 (作为局部 Y 轴)
    float2 yAxis = ba / h;
    // 垂直轴向 (作为局部 X 轴)
    float2 xAxis = float2(-yAxis.y, yAxis.x);
    
    // local_x = dot(pa, xAxis)  (点到轴线的垂直距离)
    // local_y = dot(pa, yAxis)  (点在轴线上的投影长度)
    float2 p_local = float2(dot(pa, xAxis), dot(pa, yAxis));
    
    return sdUnevenCapsule_Vertical(p_local, r1, r2, h);
}

float4 main(PS_INPUT input) : SV_Target
{
    int type = (int) (input.shapeType + 0.5);

    // 【修改 2】增加 NaN 保护 (防止 Intel 显卡画黑块)
    if (any(isnan(input.p1)) || any(isnan(input.p2)))
        discard;

    float d = 0.0;
    
    if (type == 1)
    {
        // 这里的 d 还是凸包 (没有挖空)
        d = GetInkDist_Convex(input.pixPos, input.p1, input.p2, input.r1, input.r2);
        
        float test = 0.0f; // 偏移量
        float d_bite = (input.r2 + test) - length(input.pixPos - input.p2);
        d = max(d, d_bite); 
    }
    else
    {
        // 凸包胶囊（这个不挖空 - 需要保留）
        d = GetInkDist_Convex(input.pixPos, input.p1, input.p2, input.r1, input.r2);
    }

    // 抗锯齿
    //float aa = fwidth(d);
    //aa = max(aa, 0.0001);
    //float alpha = 1.0 - smoothstep(-0.5 * aa, 0.5 * aa, d);
    
    // 计算 1 个物理像素对应的距离变化量
    float aa = fwidth(d);

    // 核心公式：0.5 - d / aa
    // d=0 (边缘) -> alpha=0.5
    // d=0.5*aa (外侧半像素) -> alpha=0.0
    // d=-0.5*aa (内侧半像素) -> alpha=1.0
    float alpha = saturate(0.5 - d / aa);
    
    if (alpha <= 0.0) discard;
    return float4(input.color.rgb, input.color.a * alpha);
}
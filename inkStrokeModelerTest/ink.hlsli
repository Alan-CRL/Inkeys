// ink.hlsli

cbuffer ScreenBuffer : register(b0)
{
    float screenWidth;
    float screenHeight;
    float2 padding;
};

// 顶点输入结构
// 必须严格匹配 C++ 中的 Input Layout 定义
struct VS_INPUT
{
    // --- Slot 0: 模板数据 (Per Vertex) ---
    // 对应 C++ 的 templateVB
    float2 templatePos : POSITION; // 0.0 ~ 1.0
    
    // --- Slot 1: 实例数据 (Per Instance) ---
    // 对应 C++ 的 instanceVB (InkVertex 32字节版)
    
    // Offset: 0
    float2 p1 : VAL_START;
    
    // Offset: 8
    float2 p2 : VAL_END;
    
    // Offset: 16
    float r1 : VAL_RAD_START;
    
    // Offset: 20
    float r2 : VAL_RAD_END;
    
    // Offset: 24
    // C++传的是 uint(RGBA8)，Input Layout设为 UNORM
    // 硬件自动归一化为 float4 (0.0 ~ 1.0)
    float4 color : COLOR;
    
    // Offset: 28
    int shapeType : VAL_TYPE;
};

// 像素输入 (VS -> PS)
// 保持不变，负责在光栅化阶段传递数据
struct PS_INPUT
{
    float4 pos : SV_POSITION; // 裁剪空间坐标
    float2 pixPos : TEXCOORD0; // 屏幕像素世界坐标 (用于计算 SDF)
    float4 color : COLOR; // 颜色
    
    // 不进行插值的数据 (nointerpolation)
    // 因为同一个胶囊内的几何属性是恒定的
    nointerpolation float2 p1 : VAL_START;
    nointerpolation float2 p2 : VAL_END;
    nointerpolation float r1 : VAL_RAD_START;
    nointerpolation float r2 : VAL_RAD_END;
    nointerpolation int shapeType : VAL_TYPE;
};
// ink.hlsli

cbuffer ScreenBuffer : register(b0)
{
    float screenWidth;
    float screenHeight;
    float2 padding;
};

// VS_INPUT: 必须与 C++ Input Layout 严格对应
struct VS_INPUT
{
    float2 templatePos : POSITION;
    float2 p1 : VAL_START;
    float2 p2 : VAL_END;
    float r1 : VAL_RAD_START;
    float r2 : VAL_RAD_END;
    float4 color : COLOR;
    float shapeType : VAL_TYPE; // 【修改：接收 float】
};

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float2 pixPos : TEXCOORD0;
    float4 color : COLOR;
    
    nointerpolation float2 p1 : VAL_START;
    nointerpolation float2 p2 : VAL_END;
    nointerpolation float r1 : VAL_RAD_START;
    nointerpolation float r2 : VAL_RAD_END;
    
    // 【关键】：即使是离散值，在旧显卡上也建议用 float 传输，
    // 防止光栅化器插值器出现未定义的行为。
    nointerpolation float shapeType : VAL_TYPE;
};
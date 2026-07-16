// ink.hlsli

// 1. 常量缓冲区 (b0)
cbuffer ScreenBuffer : register(b0)
{
    float screenWidth;
    float screenHeight;
    float globalShapeType;
    
    // 接收传来的环形缓冲偏移量
    uint globalBufferOffset;
    
    float4 globalColor;
};

// 2. 结构定义
struct InkPoint
{
    float2 pos;
    float r;
    float time;
    float2 direction;
};

// 3. 结构化缓冲区
StructuredBuffer<InkPoint> InkData : register(t0);
Texture2D AlphaBlendSource : register(t1);
Texture2D AuxiliaryBlendSource : register(t2); // 双遮罩合成时提供 L0 实时覆盖率。
SamplerState AlphaBlendSampler : register(s0);

// 4. VS -> PS
struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float2 pixPos : TEXCOORD0;
    float2 uv : TEXCOORD1;
    
    nointerpolation float4 color : COLOR;
    
    nointerpolation float2 p1 : VAL_START;
    nointerpolation float2 p2 : VAL_END;
    nointerpolation float r1 : VAL_RAD_START;
    nointerpolation float r2 : VAL_RAD_END;
    nointerpolation float2 direction1 : TEXCOORD2;
    nointerpolation float2 direction2 : TEXCOORD3;
    nointerpolation float shapeType : VAL_TYPE;
};

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
    uint globalOperatorKind;
    float3 globalPadding;
};

// Laser 独立材质常量：radii=(基准白芯半径, 基准实体半径, 固定漫反射宽度, 基准散射半宽)。
cbuffer LaserStyleBuffer : register(b1)
{
    float4 laserRadii;
    float4 laserCoreColor;
    float4 laserScatterColor;
    float4 laserBorderColor;
    float4 laserEdgeColor;
    float4 laserGlowColor;
    float4 laserParameters;
};

// 2. 结构定义
struct InkPoint
{
    float2 pos;
    float r;
    float time;
};

struct HighlighterPrimitive
{
    float2 p1;
    float2 p2;
    float2 halfSize;
};

// 3. 结构化缓冲区
StructuredBuffer<InkPoint> InkData : register(t0);
Texture2D StableOperatorAdd : register(t1);
Texture2D StableOperatorRetain : register(t2);
StructuredBuffer<HighlighterPrimitive> HighlighterData : register(t3);
Texture2D LiveOperatorAdd : register(t4);
Texture2D LiveOperatorRetain : register(t5);
Texture2D LaserCompositedColor : register(t6);
Texture2D LaserStrokeCoverage : register(t7);
SamplerState OperatorSampler : register(s0);

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
    nointerpolation float shapeType : VAL_TYPE;
};

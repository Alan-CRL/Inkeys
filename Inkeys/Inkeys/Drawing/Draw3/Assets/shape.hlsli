// Independent shape program. Existing ink b0..b3 and shape IDs are unchanged.
cbuffer ShapeBuffer : register(b4)
{
    float2 shapeScreen;
    uint shapeBufferOffset;
    uint shapePass;
    float4 shapeColor;
    float2 shapeOrigin;
    float shapeDashOffset;
    uint shapeDashKind;
    float4 shapeRect;
};

// Four float4 slots, exactly matching shapes::GpuPrimitive (64 bytes).
StructuredBuffer<float4> ShapeData : register(t0);
Texture2D ShapeAdd : register(t1);
Texture2D ShapeRetain : register(t2);

struct ShapePixelInput
{
    float4 position : SV_POSITION;
    float2 pixel : TEXCOORD0;
    nointerpolation float4 a : TEXCOORD1;
    nointerpolation float4 b : TEXCOORD2;
    nointerpolation float4 c : TEXCOORD3;
    nointerpolation float4 d : TEXCOORD4;
};

float2 ShapeCorner(uint vertex)
{
    if (vertex == 0 || vertex == 3) return float2(0, 0);
    if (vertex == 1) return float2(1, 0);
    if (vertex == 2 || vertex == 4) return float2(1, 1);
    return float2(0, 1);
}

float ShapeCross(float2 a, float2 b) { return a.x * b.y - a.y * b.x; }

// imgui_common.hlsli

// 常量缓冲区 (Register b0)
cbuffer vertexBuffer : register(b0)
{
    float4x4 ProjectionMatrix;
};

// 顶点着色器输入
struct VS_INPUT
{
    float2 pos : POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
};

// 顶点着色器输出 / 像素着色器输入
struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
};
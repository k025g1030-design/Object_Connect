cbuffer CanvasConstants : register(b0)
{
    float canvasWidth;
    float canvasHeight;
};

struct VertexShaderInput
{
    float2 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};


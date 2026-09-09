cbuffer CanvasConstants : register(b0) {
    float canvasLeft;
    float canvasTop;
    float canvasWidth;
    float canvasHeight;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

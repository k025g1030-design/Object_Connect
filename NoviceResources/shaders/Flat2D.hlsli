#pragma pack_matrix(row_major)

cbuffer CanvasConstants : register(b0) {
    float canvasWidth;
    float canvasHeight;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

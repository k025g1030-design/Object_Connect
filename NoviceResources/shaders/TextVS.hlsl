#include "Text.hlsli"

VSOutput main(float2 position : POSITION, float2 uv : TEXCOORD0,
              float4 color : COLOR0) {
    VSOutput output;
    const float2 ndc = float2(
        (position.x - canvasLeft) / canvasWidth * 2.0f - 1.0f,
        1.0f - (position.y - canvasTop) / canvasHeight * 2.0f);
    output.position = float4(ndc, 0.0f, 1.0f);
    output.uv = uv;
    output.color = color;
    return output;
}

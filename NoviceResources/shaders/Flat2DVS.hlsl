#include "Flat2D.hlsli"

VSOutput main(float2 position : POSITION, float4 color : COLOR) {
    VSOutput output;
    const float2 ndc = float2(
        position.x / canvasWidth * 2.0f - 1.0f,
        1.0f - position.y / canvasHeight * 2.0f);
    output.position = float4(ndc, 0.0f, 1.0f);
    output.color = color;
    return output;
}

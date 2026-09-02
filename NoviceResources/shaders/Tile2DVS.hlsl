#include "Tile2D.hlsli"

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    output.position = float4(
        input.position.x * (2.0f / canvasWidth) - 1.0f,
        1.0f - input.position.y * (2.0f / canvasHeight),
        0.0f,
        1.0f);
    output.texcoord = input.texcoord;
    output.color = input.color;
    return output;
}


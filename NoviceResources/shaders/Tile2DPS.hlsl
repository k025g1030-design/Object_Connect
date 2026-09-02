#include "Tile2D.hlsli"

Texture2D<float4> tileAtlas : register(t0);
SamplerState pointSampler : register(s0);

float4 main(VertexShaderOutput input) : SV_TARGET
{
    return tileAtlas.Sample(pointSampler, input.texcoord) * input.color;
}

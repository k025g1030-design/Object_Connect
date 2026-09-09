#include "Text.hlsli"

Texture2D<float> glyphAtlas : register(t0);
SamplerState glyphSampler : register(s0);

float4 main(VSOutput input) : SV_TARGET {
    const float coverage = glyphAtlas.Sample(glyphSampler, input.uv);
    return float4(input.color.rgb, input.color.a * coverage);
}

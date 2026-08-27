Texture2D<float4> sceneTexture : register(t0);
SamplerState sceneSampler : register(s0);

cbuffer ScenePostProcessConstants : register(b0) {
    float brightness;
    float gamma;
};

float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float3 linearColor = sceneTexture.Sample(sceneSampler, uv).rgb;
    linearColor = saturate(linearColor * brightness);
    linearColor = pow(linearColor, 2.2f / gamma);
    return float4(linearColor, 1.0f);
}

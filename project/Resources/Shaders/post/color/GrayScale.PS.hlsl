#include "GrayScale.hlsli"

Texture2D <float4>gTexture: register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    output.color = gTexture.Sample(gSampler, input.texcoord);
    //グレースケール
    float value = dot(output.color.rgb, float3(0.299, 0.587, 0.114));
    output.color.rgb = value * float3(1.0f, 74.0f / 107.0f, 43.0f / 107.0f);
    return output;
}
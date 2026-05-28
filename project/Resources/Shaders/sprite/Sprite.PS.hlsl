#include "Sprite.hlsli"

struct Material
{
    float4 color;
    float4x4 uvTransform;
};

ConstantBuffer<Material> gMaterial : register(b0);

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    // UV座標の変換
    float4 transformdUV = mul(float4(input.texcoord, 0.0, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformdUV.xy);

    PixelShaderOutput output;
    // 照明を削除し、単純な色とテクスチャの掛け算に変更
    output.color = gMaterial.color * textureColor;

    return output;
}

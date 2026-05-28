#include "Fullscreen.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    // ---- 手打ち定数 ----
    float2 kCenter = float2(0.5f, 0.5f); // 画面中心（中央固定）
    float kBlurWidth = 0.01f; // ブラーの強さ（広がり）
    int kNumSamples = 10; // サンプル数（滑らかさ）
    // --------------------

    float2 direction = kCenter - input.texcoord; // 中心からの方向
    float3 outputColor = float3(0.0f, 0.0f, 0.0f);

    // サンプルの合成
    for (int i = 0; i < kNumSamples; ++i)
    {
        float2 offset = direction * kBlurWidth * i;
        float2 sampleUV = input.texcoord + offset;
        outputColor += gTexture.Sample(gSampler, sampleUV).rgb;
    }

    // 平均化
    outputColor /= kNumSamples;

    PixelShaderOutput output;
    output.color = float4(outputColor, 1.0f);
    return output;
}
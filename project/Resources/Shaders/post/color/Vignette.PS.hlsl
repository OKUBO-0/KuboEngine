#include "Vignette.hlsli"

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
    
    //周囲を０に、中心になるほど明るくくなるよう計算調整
    float2 correct = input.texcoord * (1.0f - input.texcoord.yx);
    //correctだけで計算すると中心の最大値が0.0625で暗すぎるのでScaleで調整
    float vignette = correct.x * correct.y * 16.0f;
    //とりあえず0.8乗でそれっぽくしてみた
    vignette = saturate(pow(vignette, 0.8f));
    //係数として乗算
    output.color.rgb *= vignette;
    
    return output;
}
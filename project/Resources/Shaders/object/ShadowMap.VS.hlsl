struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
    float4x4 WorldInverseTranspose;
};

struct ShadowMapData
{
    float4x4 lightViewProjection;
    float4 settings;
};

ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);
ConstantBuffer<ShadowMapData> gShadowData : register(b4);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

float4 main(VertexShaderInput input) : SV_POSITION
{
    float4 worldPosition = mul(input.position, gTransformationMatrix.World);
    return mul(worldPosition, gShadowData.lightViewProjection);
}

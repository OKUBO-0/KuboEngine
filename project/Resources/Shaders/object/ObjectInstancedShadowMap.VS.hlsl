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

struct InstanceData
{
    TransformationMatrix transform;
    float4 color;
};

ConstantBuffer<ShadowMapData> gShadowData : register(b4);
StructuredBuffer<InstanceData> gInstances : register(t3);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

float4 main(VertexShaderInput input, uint instanceId : SV_InstanceID) : SV_POSITION
{
    InstanceData instanceData = gInstances[instanceId];
    float4 worldPosition = mul(input.position, instanceData.transform.World);
    return mul(worldPosition, gShadowData.lightViewProjection);
}

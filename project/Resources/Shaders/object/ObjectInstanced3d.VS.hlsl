#include "Object3d.hlsli"

struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
    float4x4 WorldInverseTranspose;
};

struct InstanceData
{
    TransformationMatrix transform;
    float4 color;
};

ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);
StructuredBuffer<InstanceData> gInstances : register(t3);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input, uint instanceId : SV_InstanceID)
{
    VertexShaderOutput output;
    InstanceData instanceData = gInstances[instanceId];
    TransformationMatrix transformationMatrix = instanceData.transform;
    output.position = mul(input.position, transformationMatrix.WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float3x3) transformationMatrix.WorldInverseTranspose));
    output.worldPosition = mul(input.position, transformationMatrix.World).xyz;
    output.instanceColor = instanceData.color;
    return output;
}

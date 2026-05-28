#include "Line.hlsli"

cbuffer CameraCB : register(b0)
{
    float4x4 view;
    float4x4 projection;
};



struct LineInstanceData
{
    float3 start;
    float3 end;
    float4 color;
    
};

// StructuredBuffer でインスタンスごとのデータを渡す
StructuredBuffer<LineInstanceData> gLineInstances : register(t0);

struct VertexShaderInput
{
    float3 position : POSITION0; // 頂点位置（ローカル）
};

VertexShaderOutput main(VertexShaderInput input, uint instanceId : SV_InstanceID)
{
    VertexShaderOutput output;

    LineInstanceData data = gLineInstances[instanceId];

    float3 dir = data.end - data.start;
    float3 worldPos = data.start + input.position.x * dir;

    float4 viewPos = mul(float4(worldPos, 1.0f), view);
    output.position = mul(viewPos, projection);
    output.color = data.color;

    return output;
}

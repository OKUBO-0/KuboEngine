#include "BoxFilter.hlsli"

static const uint32_t kNumVertex = 3;
static const float4 kPositions[kNumVertex] =
{
    float4(-1.0f, -1.0f, 0.0f, 1.0f),
    float4(-1.0f, 3.0f, 0.0f, 1.0f),
    float4(3.0f, -1.0f, 0.0f, 1.0f)
};

static const float2 kTexCoords[kNumVertex] =
{
    float2(0.0f, 1.0f),
    float2(0.0f, -1.0f),
    float2(2.0f, 1.0f)
};

VertexShaderOutput main(uint32_t vertexId : SV_VertexID)
{
    VertexShaderOutput output;
    output.position = kPositions[vertexId];
    output.texcoord = kTexCoords[vertexId];
    return output;
}
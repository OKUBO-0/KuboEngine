struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOOD0;
    float3 normal : NORMAL0;
    float3 worldPosition : POSITION0;
    float4 instanceColor : COLOR0;
};


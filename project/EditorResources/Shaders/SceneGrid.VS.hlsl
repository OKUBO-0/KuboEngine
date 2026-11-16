struct GridVertex
{
    float4 position : SV_Position;
    float2 clipXY : TEXCOORD0;
};

GridVertex main(uint vertex : SV_VertexID)
{
    GridVertex output;
    output.clipXY = float2(vertex == 2 ? 3 : -1, vertex == 1 ? 3 : -1);
    output.position = float4(output.clipXY, 0, 1);
    return output;
}

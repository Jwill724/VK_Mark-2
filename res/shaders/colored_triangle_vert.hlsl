struct Colored_Triangle
{
    float4 pos : SV_Position;
    float3 color : COLOR0;
};

Colored_Triangle main(uint vertexID : SV_VertexID)
{
    Colored_Triangle output;

    float3 positions[3] =
    {
        float3(1.0f, 1.0f, 0.0f),
        float3(-1.0f, 1.0f, 0.0f),
        float3(0.0f, -1.0f, 0.0f)
    };

    float3 colors[3] =
    {
        float3(1.0f, 0.0f, 0.0f), // red
        float3(0.0f, 1.0f, 0.0f), // green
        float3(0.0f, 0.0f, 1.0f) // blue
    };

    // output the position and color of each vertex
    output.pos = float4(positions[vertexID], 1.0f);
    output.color = colors[vertexID];

    return output;
}
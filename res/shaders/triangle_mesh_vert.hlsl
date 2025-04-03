struct VSOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
    float2 uv : TEXCOORD0;
};

struct Vertex
{
    float3 pos;
    float uv_x;
    float3 normal;
    float uv_y;
    float4 color;
};

//cbuffer SceneData : register(b0)
//{
//    float4x4 view;
//    float4x4 proj;
//    float4x4 viewproj;
//    float4 ambientColor;
//    float4 sunlightDirection;
//    float4 sunlightColor;
//};

struct PushConstants
{
    float4x4 render_matrix;
    uint2 vertexBufferAddress;
};

[[vk::push_constant]]
PushConstants pc;

StructuredBuffer<Vertex> vertexBuffer;

VSOutput main(uint vertexID : SV_VertexID)
{
    Vertex v = vertexBuffer[vertexID];

    VSOutput o;
    // Transform position
    o.position = mul(pc.render_matrix, float4(v.pos, 1.0));
    // Output data
    o.color = v.color.xyz;
    o.uv = float2(v.uv_x, v.uv_y);

    return o;
}
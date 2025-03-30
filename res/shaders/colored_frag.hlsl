struct Colored_frag
{
    float3 color : COLOR0;
};

float4 main(Colored_frag input) : SV_TARGET
{
    return float4(input.color, 1.0f);
}
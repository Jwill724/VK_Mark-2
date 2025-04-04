// Texture and sampler declarations
Texture2D displayTexture : register(t1);
SamplerState sampler0 : register(s1);

struct TexColor
{
    float3 inColor : COLOR0;
    float2 inUV    : TEXTURE0;
};

float4 main(TexColor input) : SV_TARGET
{
    return displayTexture.Sample(sampler0, input.inUV) * float4(input.inColor, 1.0f);
}
// In HLSL, read/write images are declared as RWTexture2D.
// "float4" is the pixel format in the shader; the actual Vulkan format (rgba16f)
// is set in your VkImage / pipeline config, not here.
RWTexture2D<float4> image0 : register(u0);

// Push constants do not exist as a native concept in HLSL,
// but you can declare a cbuffer and then map it to push constants in Vulkan
// via reflection or specialized pipeline setup.
struct PushConstants
{
    float4 data1;
    float4 data2;
    float4 data3; // included to match your push constant size
    float4 data4; // (no direct usage, just padding if you like)
};

[[vk::push_constant]]
PushConstants pc;

// HLSL version of "layout(local_size_x = 16, local_size_y = 16)"
// The [numthreads(16,16,1)] attribute sets your compute workgroup size.
[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // DTid.x / DTid.y / DTid.z are the global invocation IDs
    uint2 texelCoord = DTid.xy;

    // Get the size of the image
    uint width, height;
    image0.GetDimensions(width, height);

    // Equivalent to your "topColor" and "bottomColor" in GLSL
    float4 topColor = pc.data1;
    float4 bottomColor = pc.data2;

    // Bounds check, just like "if (texelCoord.x < size.x && ...)"
    if (texelCoord.x < width && texelCoord.y < height)
    {
        // Blend factor along Y
        float blend = (float) (texelCoord.y) / (float) height;

        // Lerp in HLSL is "lerp(a, b, t)" -> same as mix() in GLSL
        float4 finalColor = lerp(topColor, bottomColor, blend);

        // imageStore(image, texelCoord, finalColor) in GLSL -> image0[texelCoord] = finalColor
        image0[texelCoord] = finalColor;
    }
}
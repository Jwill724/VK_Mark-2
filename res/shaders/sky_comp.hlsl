RWTexture2D<float4> image0 : register(u0);
Texture2D<float4> inputImage : register(t1); // Read source
SamplerState inputSampler : register(s1); // Sampler

struct PushConstants
{
    float4 data1;
    float4 data2;
    float4 data3;
    float4 data4;
};

[[vk::push_constant]]
PushConstants pc;

// Return random noise in the range [0.0, 1.0], as a function of vec.
float Noise2d(float2 vec)
{
    float xhash = cos(vec.x * 37.0f);
    float yhash = cos(vec.y * 57.0f);

    return frac(415.92653f * (xhash + yhash));
}

// Convert Noise2d() into a "star field" by stomping everthing below fThreshhold to zero.
float NoisyStarField(float2 vSamplePos, float fThreshhold)
{
    float StarVal = Noise2d(vSamplePos);
    if (StarVal >= fThreshhold)
        StarVal = pow((StarVal - fThreshhold) / (1.0f - fThreshhold), 6.0f);
    else
        StarVal = 0.0f;
    return StarVal;
}

// Stabilize NoisyStarField() by only sampling at integer values.
float StableStarField(float2 vSamplePos, float fThreshhold)
{
    // Linear interpolation between four samples.
    // Note: This approach has some visual artifacts.
    // There must be a better way to "anti alias" the star field.
    float fractX = frac(vSamplePos.x);
    float fractY = frac(vSamplePos.y);
    float2 floorSample = floor(vSamplePos);
    float v1 = NoisyStarField(floorSample, fThreshhold);
    float v2 = NoisyStarField(floorSample + float2(0.0f, 1.0f), fThreshhold);
    float v3 = NoisyStarField(floorSample + float2(1.0f, 0.0f), fThreshhold);
    float v4 = NoisyStarField(floorSample + float2(1.0f, 1.0f), fThreshhold);

    float StarVal = v1 * (1.0f - fractX) * (1.0f - fractY)
        			+ v2 * (1.0f - fractX) * fractY
        			+ v3 * fractX * (1.0f - fractY)
        			+ v4 * fractX * fractY;

    return StarVal;
}

void mainImage(out float4 fragColor, float2 fragCoord)
{
    // Get image dimensions and form a resolution vector.
    uint width, height;
    image0.GetDimensions(width, height);
    float2 iResolution = float2(width, height);

	// Sky Background Color
	//float3 vColor = float3( 0.1f, 0.2f, 0.4f ) * fragCoord.y / iResolution.y;
    float3 vColor = pc.data1.xyz * (fragCoord.y / iResolution.y);

    // Note: Choose fThreshhold in the range [0.99, 0.9999].
    // Higher values (i.e., closer to one) yield a sparser starfield.
    float StarFieldThreshhold = pc.data1.w; //0.97;

    // Define star movement rates.
    float xRate = 0.2f;
    float yRate = -0.06f;
    // Add a small offset for a "crawling" effect.
    float2 vSamplePos = fragCoord + float2(xRate, yRate);

    // Compute the star field intensity.
    float starVal = StableStarField(vSamplePos, StarFieldThreshhold);
    // Add the star value uniformly to the background color.
    vColor += float3(starVal, starVal, starVal);

    float4 background = float4(vColor, 1.0f);

// Add stars on top of the gradient
    background.rgb += starVal;

// Blend background + geometry based on alpha (or let geometry sit on top if opaque)
    float4 previousColor = inputImage.SampleLevel(inputSampler, fragCoord / iResolution, 0.0f);

// Just alpha-blend the background under the mesh
    fragColor = lerp(background, previousColor, previousColor.a);

    //float3 bgColor = vColor + float3(starVal, starVal, starVal);
    //float3 geometry = inputImage.SampleLevel(inputSampler, fragCoord / iResolution, 1.f).rgb;

    //float blendAmount = 0.8f;
    //float3 result = lerp(bgColor, geometry, blendAmount);

    //fragColor = float4(result, 1.0f);
}

[numthreads(16, 16, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    uint2 texelCoord = DTid.xy;

    uint width, height;
    image0.GetDimensions(width, height);

    // Ensure we are within the image bounds.
    if (texelCoord.x < width && texelCoord.y < height)
    {
        float4 color;
        // Call mainImage() to compute the pixel color at the given coordinate.
        mainImage(color, float2(texelCoord));
        // Write the computed color to the image.
        image0[texelCoord] = color;
    }
}
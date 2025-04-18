#version 450

layout(location = 0) in vec3 fragDir;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform samplerCube skyboxSampler;

void main()
{
    vec3 dir = normalize(fragDir);

     // Flip the image
     dir.y = -dir.y;

    // fetch the HDR/RGB color
    vec3 color = texture(skyboxSampler, dir).rgb;

    outColor = vec4(color, 1.0);
}
#ifndef SHADING_FUNCTIONS_GLSL
#define SHADING_FUNCTIONS_GLSL

// Specular AA
// Reduce sparkling/aliasing of specular highlights caused by
// high-frequency normal variation
float specularAA(float roughness, vec3 N)
{
	vec3 dndx = dFdxFine(N);
	vec3 dndy = dFdyFine(N);

	float normalVariance = dot(dndx, dndx) + dot(dndy, dndy);

	float filteredRoughness2 = roughness * roughness + normalVariance;

	return clamp(sqrt(filteredRoughness2), 0.04, 1.0);
}


// How light-space depth changes per unit atlas-UV on the receiver plane.
vec2 computeDepthGradientUV(vec2 atlasUV, float depth)
{
	vec2  duvdx = dFdx(atlasUV);
	vec2  duvdy = dFdy(atlasUV);
	float dzdx  = dFdx(depth);
	float dzdy  = dFdy(depth);

	float det = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
	if (abs(det) < 1e-12) return vec2(0.0);

	vec2 g;
	g.x = ( duvdy.y * dzdx - duvdx.y * dzdy) / det;
	g.y = (-duvdy.x * dzdx + duvdx.x * dzdy) / det;
	return g;
}

#endif


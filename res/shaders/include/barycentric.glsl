#ifndef BARYCENTRIC_GLSL
#define BARYCENTRIC_GLSL

// ======================================================================
// Analytic perspective-correct barycentrics + screen-space derivatives.

struct BarycentricDeriv
{
	vec3 m_lambda; // barycentric weights at the pixel
	vec3 m_ddx;    // d(lambda)/dx in screen space
	vec3 m_ddy;    // d(lambda)/dy in screen space
};

// pt0/pt1/pt2 : clip-space positions of the triangle's three vertices
// pixelNdc    : this pixel's NDC position, xy in [-1, 1]
// winSize     : viewport size in pixels
BarycentricDeriv CalcFullBary(
	vec4 pt0,
	vec4 pt1,
	vec4 pt2,
	vec2 pixelNdc,
	vec2 winSize)
{
	BarycentricDeriv ret;

	vec3 invW = 1.0 / vec3(pt0.w, pt1.w, pt2.w);

	vec2 ndc0 = pt0.xy * invW.x;
	vec2 ndc1 = pt1.xy * invW.y;
	vec2 ndc2 = pt2.xy * invW.z;

	// Inverse area of the projected triangle. Degenerate/backfacing triangles make this blow up
	float invDet = 1.0 / determinant(mat2(ndc2 - ndc1, ndc0 - ndc1));

	ret.m_ddx = vec3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet * invW;
	ret.m_ddy = vec3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet * invW;

	float ddxSum = dot(ret.m_ddx, vec3(1.0));
	float ddySum = dot(ret.m_ddy, vec3(1.0));

	vec2  deltaVec    = pixelNdc - ndc0;
	float interpInvW  = invW.x + deltaVec.x * ddxSum + deltaVec.y * ddySum;
	float interpW     = 1.0 / interpInvW;

	ret.m_lambda = vec3(
		interpW * (invW.x + deltaVec.x * ret.m_ddx.x + deltaVec.y * ret.m_ddy.x),
		interpW * (0.0    + deltaVec.x * ret.m_ddx.y + deltaVec.y * ret.m_ddy.y),
		interpW * (0.0    + deltaVec.x * ret.m_ddx.z + deltaVec.y * ret.m_ddy.z)
	);

	// Convert NDC-space derivatives to pixel-space derivatives.
	ret.m_ddx *= (2.0 / winSize.x);
	ret.m_ddy *= (2.0 / winSize.y);
	ddxSum    *= (2.0 / winSize.x);
	ddySum    *= (2.0 / winSize.y);

	// Y flip: NDC Y grows opposite to pixel Y.
	ret.m_ddy *= -1.0;
	ddySum    *= -1.0;

	float interpW_ddx = 1.0 / (interpInvW + ddxSum);
	float interpW_ddy = 1.0 / (interpInvW + ddySum);

	ret.m_ddx = interpW_ddx * (ret.m_lambda * interpInvW + ret.m_ddx) - ret.m_lambda;
	ret.m_ddy = interpW_ddy * (ret.m_lambda * interpInvW + ret.m_ddy) - ret.m_lambda;

	return ret;
}

// Guarded variant. Degenerate projected triangles (zero area) produce Inf/NaN through invDet. 
bool CalcFullBarySafe(
	vec4 pt0,
	vec4 pt1,
	vec4 pt2,
	vec2 pixelNdc,
	vec2 winSize,
	out BarycentricDeriv outBary)
{
	vec3 invW = 1.0 / vec3(pt0.w, pt1.w, pt2.w);
	vec2 ndc0 = pt0.xy * invW.x;
	vec2 ndc1 = pt1.xy * invW.y;
	vec2 ndc2 = pt2.xy * invW.z;

	float det = determinant(mat2(ndc2 - ndc1, ndc0 - ndc1));
	if (abs(det) < 1e-12)
	{
		outBary.m_lambda = vec3(1.0, 0.0, 0.0);
		outBary.m_ddx    = vec3(0.0);
		outBary.m_ddy    = vec3(0.0);
		return false;
	}

	outBary = CalcFullBary(pt0, pt1, pt2, pixelNdc, winSize);
	return true;
}

// =======================
// Interpolation helpers

float InterpolateFloat(BarycentricDeriv d, float v0, float v1, float v2)
{
	return dot(vec3(v0, v1, v2), d.m_lambda);
}

vec3 InterpolateVec3(BarycentricDeriv d, vec3 v0, vec3 v1, vec3 v2)
{
	return vec3(
		dot(vec3(v0.x, v1.x, v2.x), d.m_lambda),
		dot(vec3(v0.y, v1.y, v2.y), d.m_lambda),
		dot(vec3(v0.z, v1.z, v2.z), d.m_lambda)
	);
}

vec4 InterpolateVec4(BarycentricDeriv d, vec4 v0, vec4 v1, vec4 v2)
{
	return vec4(
		dot(vec4(v0.x, v1.x, v2.x, 0.0).xyz, d.m_lambda),
		dot(vec4(v0.y, v1.y, v2.y, 0.0).xyz, d.m_lambda),
		dot(vec4(v0.z, v1.z, v2.z, 0.0).xyz, d.m_lambda),
		dot(vec4(v0.w, v1.w, v2.w, 0.0).xyz, d.m_lambda)
	);
}

// UV plus its screen-space gradients, ready for textureGrad().
struct UVGrad
{
	vec2 uv;
	vec2 ddx;
	vec2 ddy;
};

UVGrad InterpolateUVGrad(BarycentricDeriv d, vec2 uv0, vec2 uv1, vec2 uv2)
{
	vec3 u = vec3(uv0.x, uv1.x, uv2.x);
	vec3 v = vec3(uv0.y, uv1.y, uv2.y);

	UVGrad r;
	r.uv  = vec2(dot(u, d.m_lambda), dot(v, d.m_lambda));
	r.ddx = vec2(dot(u, d.m_ddx),    dot(v, d.m_ddx));
	r.ddy = vec2(dot(u, d.m_ddy),    dot(v, d.m_ddy));
	return r;
}

vec2 PixelToNdc(uvec2 gid, vec2 winSize)
{
	vec2 uv = (vec2(gid) + 0.5) / winSize;
	return vec2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
}

#endif

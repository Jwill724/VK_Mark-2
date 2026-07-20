#ifndef CULLING_GLSL
#define CULLING_GLSL

#extension GL_GOOGLE_include_directive  : require
#include "depth.glsl"

// Pretty much copied this
// https://github.com/PanosK92/SpartanEngine/blob/master/data/shaders/common_culling.hlsl

// extracts the four side planes of the camera frustum from view_projection in world space
// only the side planes are used, near is unreliable on jittered projections and far is at infinity for reverse-z
void get_frustum_side_planes(const mat4 vp, out vec4 plane_l, out vec4 plane_r, out vec4 plane_b, out vec4 plane_t)
{
	// extract rows from column-major mat4
	vec4 row0 = vec4(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
	vec4 row1 = vec4(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
	vec4 row3 = vec4(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);

	plane_l = row3 + row0;
	plane_r = row3 - row0;
	plane_b = row3 + row1;
	plane_t = row3 - row1;

	// normalize so the radius compare lives in world units, the planes get reused for every task in the wave so doing it once is fine
	plane_l /= max(length(plane_l.xyz), 1e-8);
	plane_r /= max(length(plane_r.xyz), 1e-8);
	plane_b /= max(length(plane_b.xyz), 1e-8);
	plane_t /= max(length(plane_t.xyz), 1e-8);
}

bool sphere_in_side_planes(vec3 center, float radius, vec4 plane_l, vec4 plane_r, vec4 plane_b, vec4 plane_t)
{
	float dl = dot(plane_l.xyz, center) + plane_l.w;
	float dr = dot(plane_r.xyz, center) + plane_r.w;
	float db = dot(plane_b.xyz, center) + plane_b.w;
	float dt = dot(plane_t.xyz, center) + plane_t.w;
	float min_dist = min(min(dl, dr), min(db, dt));
	return min_dist >= -radius;
}


// shared mip pick + 4-corner depth gather, the box uvs are in full-texture [0,1] space,
// dropping the center sample is safe because the chosen mip ensures the box fits in roughly one texel and the four corner
// reads cover every texel the box can touch after the one-texel border expansion below
float hiz_min_depth_over_box(usampler2D hiz, vec2 min_uv, vec2 max_uv, float max_mip_level)
{
	vec2 render_size = vec2(textureSize(hiz, 0));

	vec2 uv_extent = max_uv - min_uv;
	vec2 size_px   = uv_extent * render_size;
	float mip      = ceil(log2(max(max(size_px.x, size_px.y), 1.0)));
	mip            = clamp(mip, 0.0, max_mip_level);

	vec2 mip_texel = exp2(mip) / render_size;
	min_uv = clamp(min_uv - mip_texel, 0.0, 1.0);
	max_uv = clamp(max_uv + mip_texel, 0.0, 1.0);

	vec4 uvs = vec4(min_uv, max_uv);

	float minD, maxD;
	float d0, d1, d2, d3;

	unpackHZB(textureLod(hiz, uvs.xy, mip).r, minD, maxD); d0 = minD;
	unpackHZB(textureLod(hiz, uvs.zy, mip).r, minD, maxD); d1 = minD;
	unpackHZB(textureLod(hiz, uvs.xw, mip).r, minD, maxD); d2 = minD;
	unpackHZB(textureLod(hiz, uvs.zw, mip).r, minD, maxD); d3 = minD;

	// furthest occluder in reverse-z = smallest depth value
	return min(min(d0, d1), min(d2, d3));
}

// fast analytical hi-z for a world-space sphere
// projects the center once and derives a conservative ndc rectangle from the row-axis sensitivities of view_projection
// this replaces the per-thread 8-corner cube projection method and the bound is also tighter than the cube
// projection that surrounds the sphere, so it culls more aggressively and chooses a smaller hi-z footprint
bool sphere_hiz_visible(usampler2D hiz, vec3 center_world, float radius_world, float max_mip_level, mat4 vp)
{
	// partials of clip-space output w.r.t. world-space input, wave uniform so dxc keeps these in scalar registers
	vec3 ax_x = vec3(vp[0][0], vp[1][0], vp[2][0]);
	vec3 ax_y = vec3(vp[0][1], vp[1][1], vp[2][1]);
	vec3 ax_z = vec3(vp[0][2], vp[1][2], vp[2][2]);
	vec3 ax_w = vec3(vp[0][3], vp[1][3], vp[2][3]);

	float ax_x_len = length(ax_x);
	float ax_y_len = length(ax_y);
	float ax_z_len = length(ax_z);
	float ax_w_len = length(ax_w);

	float cx = dot(center_world, ax_x) + vp[3][0];
	float cy = dot(center_world, ax_y) + vp[3][1];
	float cz = dot(center_world, ax_z) + vp[3][2];
	float cw = dot(center_world, ax_w) + vp[3][3];

	float rx = radius_world * ax_x_len;
	float ry = radius_world * ax_y_len;
	float rz = radius_world * ax_z_len;
	float rw = radius_world * ax_w_len;

	// sphere entirely behind the camera, side-frustum has already rejected this so this is a paranoia branch
	if (cw + rw <= 0.0) return false;

	// sphere straddles the near plane, the perspective divide is unstable so skip occlusion conservatively
	if (cw - rw <= 0.0) return true;

	// each ndc extreme is one of four (numerator extreme) * (1 / denominator extreme), enumerate and reduce
	float inv_w_close = 1.0 / (cw - rw);
	float inv_w_far   = 1.0 / (cw + rw);

	float xlc = (cx - rx) * inv_w_close;
	float xlf = (cx - rx) * inv_w_far;
	float xhc = (cx + rx) * inv_w_close;
	float xhf = (cx + rx) * inv_w_far;
	float ylc = (cy - ry) * inv_w_close;
	float ylf = (cy - ry) * inv_w_far;
	float yhc = (cy + ry) * inv_w_close;
	float yhf = (cy + ry) * inv_w_far;

	vec2 min_ndc = vec2(min(min(xlc, xlf), min(xhc, xhf)), min(min(ylc, ylf), min(yhc, yhf)));
	vec2 max_ndc = vec2(max(max(xlc, xlf), max(xhc, xhf)), max(max(ylc, ylf), max(yhc, yhf)));

	// closest sphere depth in reverse-z is max numerator over min positive denominator
	float closest_box_z = (cz + rz) * inv_w_close;

	if (max_ndc.x < -1.0 || min_ndc.x > 1.0 || max_ndc.y < -1.0 || min_ndc.y > 1.0) return false;

	vec2 uv_a   = clamp((min_ndc * vec2(0.5, -0.5) + 0.5), 0.0, 1.0);
	vec2 uv_b   = clamp((max_ndc * vec2(0.5, -0.5) + 0.5), 0.0, 1.0);
	vec2 min_uv = min(uv_a, uv_b);
	vec2 max_uv = max(uv_a, uv_b);

	float furthest_z = hiz_min_depth_over_box(hiz, min_uv, max_uv, max_mip_level);
	return closest_box_z > furthest_z - 0.01;
}

// largest world-axis scale of the upper 3x3, used to lift a local-space radius into world units
// computes squared lengths first and only sqrt the winner, shaves two of the three sqrts in the hot per-task loop
float max_world_scale(mat4 m)
{
	vec3 c0 = vec3(m[0][0], m[0][1], m[0][2]);
	vec3 c1 = vec3(m[1][0], m[1][1], m[1][2]);
	vec3 c2 = vec3(m[2][0], m[2][1], m[2][2]);
	float sx_sq = dot(c0, c0);
	float sy_sq = dot(c1, c1);
	float sz_sq = dot(c2, c2);
	return sqrt(max(sx_sq, max(sy_sq, sz_sq)));
}

#endif

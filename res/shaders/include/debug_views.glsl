#ifndef DEBUG_VIEWS_GLSL
#define DEBUG_VIEWS_GLSL

// View space
const uint DBG_OFF           = 0u;
const uint DBG_ALBEDO        = 1u;
const uint DBG_NORMALS       = 2u;
const uint DBG_ROUGHNESS     = 3u;
const uint DBG_METALLIC      = 4u;
const uint DBG_EMISSIVE      = 5u;
const uint DBG_SSAO          = 6u;
const uint DBG_SS_SHADOWS    = 7u;
const uint DBG_CASCADES      = 8u;
const uint DBG_VIS_INSTANCE  = 9u;
const uint DBG_VIS_TRIANGLE  = 10u;
const uint DBG_VIS_LOD       = 11u;
const uint DBG_COUNT         = 12u;

#define DBG_BIT(v) (1u << (v))

// Vis views need the visibility buffer, which only the deferred path consumes.
const uint DBG_CAPS_FORWARD =
	DBG_BIT(DBG_OFF)       | DBG_BIT(DBG_ALBEDO)     | DBG_BIT(DBG_NORMALS)   |
	DBG_BIT(DBG_ROUGHNESS) | DBG_BIT(DBG_METALLIC)   | DBG_BIT(DBG_EMISSIVE)  |
	DBG_BIT(DBG_SSAO)      | DBG_BIT(DBG_SS_SHADOWS) | DBG_BIT(DBG_CASCADES);

const uint DBG_CAPS_DEFERRED =
	DBG_CAPS_FORWARD       |
	DBG_BIT(DBG_VIS_INSTANCE) | DBG_BIT(DBG_VIS_TRIANGLE) | DBG_BIT(DBG_VIS_LOD);

bool debugViewSupported(uint caps, uint view)
{
	return view < DBG_COUNT && (caps & DBG_BIT(view)) != 0u;
}

vec3 debugHashColor(uint v)
{
	float f = float(v);
	return vec3(hash(vec2(f, 17.0)), hash(vec2(f, 71.0)), hash(vec2(f, 131.0)));
}

vec3 debugLodColor(uint lodIdx)
{
	if (lodIdx == LOD_IDX_LOD0) return vec3(0.0, 1.0, 0.0);
	if (lodIdx == LOD_IDX_LOD1) return vec3(0.6, 1.0, 0.0);
	if (lodIdx == LOD_IDX_LOD2) return vec3(1.0, 0.7, 0.0);
	if (lodIdx == LOD_IDX_LOD3) return vec3(1.0, 0.2, 0.0);
	if (lodIdx == LOD_IDX_BASE) return vec3(0.2, 0.5, 1.0);
	return vec3(1.0, 0.0, 1.0);
}

vec3 debugCascadeOverlay(vec3 albedo, uint cascadeIdx)
{
	return mix(albedo, cascadeColor(cascadeIdx), 0.6);
}

#endif

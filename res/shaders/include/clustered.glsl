#ifndef CLUSTERED_GLSL
#define CLUSTERED_GLSL

const uint LIGHT_TYPE_DIRECTIONAL = 0u;
const uint LIGHT_TYPE_POINT       = 1u;
const uint LIGHT_TYPE_SPOT        = 2u;

const uint LIGHT_FLAG_CASTS_SPOT_SHADOW = 1u << 0;
const uint LIGHT_FLAG_FLASHLIGHT        = 1u << 1;
const uint LIGHT_FLAG_FLASHLIGHT_OFF    = 1u << 2;

// Determines switch between dispatching over clusters or lights
const uint LIGHT_THRESHOLD = 1300u;

//const uint LIGHT_LIST_STATIC_COUNT     = 2u;
//const uint LIGHT_LIST_SLOT_DIRECTIONAL = 0u;
//const uint LIGHT_LIST_SLOT_FLASHLIGHT  = 1u;

const uint LIGHT_LIST_STATIC_COUNT     = 1u;
const uint LIGHT_LIST_SLOT_FLASHLIGHT  = 0u;

//const uint MAX_VISIBLE_LIGHTS = 1024u - LIGHT_LIST_STATIC_COUNT;
const uint MAX_VISIBLE_LIGHTS = 4096u - LIGHT_LIST_STATIC_COUNT;
//const uint MAX_VISIBLE_LIGHTS = 8192u - LIGHT_LIST_STATIC_COUNT;
//const uint MAX_VISIBLE_LIGHTS = 10240u - LIGHT_LIST_STATIC_COUNT;

struct ClusterGrid {
	uvec2 tileCoord;
	uint tileIndex;
	uint sliceZ;
	uint clusterIndex;
};

uint depthToSliceLog(float Z, float nearZ, float farZ, uint numSlices)
{
	float denom = log(farZ / nearZ);

	float value =
		log(Z) * (float(numSlices) / denom) -
		float(numSlices) * log(nearZ) / denom;

	value = clamp(value, 0.0, float(numSlices) - 1e-6);
	return uint(floor(value));
}

ClusterGrid computeClusterGrid(
	vec2 coord,
	float viewDepth,
	uvec2 viewportSize,
	uint tileSizeX,
	uint tileSizeY,
	uint tileCountX,
	uint tileCountY,
	uint zSlices,
	float nearPlane,
	float farPlane)
{
	ClusterGrid result;

	uvec2 pixelCoord = uvec2(
		clamp(coord.x, 0.0, float(viewportSize.x - 1u)),
		clamp(coord.y, 0.0, float(viewportSize.y - 1u))
	);

	result.tileCoord.x = min(pixelCoord.x / tileSizeX, tileCountX - 1u);
	result.tileCoord.y = min(pixelCoord.y / tileSizeY, tileCountY - 1u);

	result.tileIndex = result.tileCoord.y * tileCountX + result.tileCoord.x;

	float clampedViewDepth = clamp(viewDepth, nearPlane + 1e-6, farPlane);
	result.sliceZ = depthToSliceLog(clampedViewDepth, nearPlane, farPlane, zSlices);
	result.sliceZ = min(result.sliceZ, zSlices - 1u);

	uint tileCount = tileCountX * tileCountY;
	result.clusterIndex = result.sliceZ * tileCount + result.tileIndex;

	return result;
}


struct LightClusterBounds
{
	uvec2 tileMin;
	uvec2 tileMax;
	uint sliceMin;
	uint sliceMax;
};

uint clampU32(int v, uint lo, uint hi)
{
	if (v < int(lo)) return lo;
	if (v > int(hi)) return hi;
	return uint(v);
}

LightClusterBounds computeLightClusterBounds(
	mat4 view,
	mat4 proj,
	vec3 lightPosWS,
	float radius,
	uvec2 viewportSize,
	uint tileSizeX,
	uint tileSizeY,
	uint tileCountX,
	uint tileCountY,
	uint zSlices,
	float nearPlane,
	float farPlane)
{
	LightClusterBounds bounds;

	uint maxTileX = max(tileCountX, 1u) - 1u;
	uint maxTileY = max(tileCountY, 1u) - 1u;

	bounds.tileMin = uvec2(0u, 0u);
	bounds.tileMax = uvec2(maxTileX, maxTileY);

	bounds.sliceMin = 0u;
	bounds.sliceMax = max(zSlices, 1u) - 1u;

	vec3 lightVS = (view * vec4(lightPosWS, 1.0)).xyz;
	float viewDepth = -lightVS.z;
	float safeViewDepth = max(viewDepth, 0.0);

	float minZ = max(safeViewDepth - radius, nearPlane);
	float maxZ = min(safeViewDepth + radius, farPlane);

	// If the sphere intersects / crosses the near plane, maxZ can fall below nearPlane in edge cases.
	if (maxZ < nearPlane) {
		maxZ = nearPlane;
	}
	if (maxZ < minZ) {
		maxZ = minZ;
	}

	bounds.sliceMin = depthToSliceLog(minZ, nearPlane, farPlane, zSlices);
	bounds.sliceMax = depthToSliceLog(maxZ, nearPlane, farPlane, zSlices);

	bounds.sliceMin = min(bounds.sliceMin, zSlices - 1u);
	bounds.sliceMax = min(bounds.sliceMax, zSlices - 1u);

	if (bounds.sliceMax < bounds.sliceMin) {
		bounds.sliceMax = bounds.sliceMin;
	}

	// If the sphere intersects the near plane, screen-rect math becomes fragile.
	// Conservatively cover the whole screen in tiles.
	bool intersectsNearPlane = (safeViewDepth - radius) <= (nearPlane + 1e-6);
	if (intersectsNearPlane) return bounds;

	vec2 ndcMin = vec2(1e30);
	vec2 ndcMax = vec2(-1e30);

	vec4 clipCenter = proj * vec4(lightVS, 1.0);
	vec2 ndcCenter = clipCenter.xy / clipCenter.w;

	float zClosest = max(safeViewDepth - radius, nearPlane + 1e-6);
	float ndcRadiusX = (radius * abs(proj[0][0])) / zClosest;
	float ndcRadiusY = (radius * abs(proj[1][1])) / zClosest;

	ndcMin = ndcCenter - vec2(ndcRadiusX, ndcRadiusY);
	ndcMax = ndcCenter + vec2(ndcRadiusX, ndcRadiusY);

	float viewportWidth  = float(viewportSize.x);
	float viewportHeight = float(viewportSize.y);

	float minPX = (ndcMin.x * 0.5 + 0.5) * viewportWidth;
	float maxPX = (ndcMax.x * 0.5 + 0.5) * viewportWidth;

	float minPY = (1.0 - (ndcMax.y * 0.5 + 0.5)) * viewportHeight;
	float maxPY = (1.0 - (ndcMin.y * 0.5 + 0.5)) * viewportHeight;

	minPX = clamp(minPX, 0.0, viewportWidth);
	maxPX = clamp(maxPX, 0.0, viewportWidth);
	minPY = clamp(minPY, 0.0, viewportHeight);
	maxPY = clamp(maxPY, 0.0, viewportHeight);

	// - 1.0 makes tile bounds more conservative and removes any screen border artifacts
	int tileMinX = int(floor(minPX / float(tileSizeX))) - 1;
	int tileMinY = int(floor(minPY / float(tileSizeY))) - 1;
	int tileMaxX = int(ceil(maxPX / float(tileSizeX)));
	int tileMaxY = int(ceil(maxPY / float(tileSizeY)));

	bounds.tileMin.x = clampU32(tileMinX, 0u, maxTileX);
	bounds.tileMin.y = clampU32(tileMinY, 0u, maxTileY);
	bounds.tileMax.x = clampU32(tileMaxX, 0u, maxTileX);
	bounds.tileMax.y = clampU32(tileMaxY, 0u, maxTileY);
	return bounds;
}

#endif

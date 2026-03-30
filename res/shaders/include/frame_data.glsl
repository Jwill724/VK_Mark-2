#ifndef FRAME_DATA_GLSL
#define FRAME_DATA_GLSL

// =====================
// === CMAA2 buffers ===
// =====================
layout(buffer_reference, scalar) buffer Cmaa2ControlBuffer {
	uint control[];
};

layout(buffer_reference, scalar) buffer Cmaa2ShapeCandidatesBuffer {
	uint pixelIDs[];
};

layout(buffer_reference, scalar) buffer Cmaa2DeferredLocationsBuffer {
	uint quadIDs[];
};

layout(buffer_reference, scalar) buffer Cmaa2DeferredItemsBuffer {
	uvec4 items[];
};

layout(buffer_reference, scalar) buffer Cmaa2DeferredHeadsBuffer {
	uint heads[];
};

// =================================
// === clustered shading buffers ===
// =================================
layout(buffer_reference, scalar) buffer VisibleLightCount {
	uint count;
};

layout(buffer_reference, scalar) buffer VisibleLightIDs {
	uint ids[];
};

layout(buffer_reference, scalar) buffer ClusterCounts {
	uint counts[];
};

layout(buffer_reference, scalar) buffer ClusterOffsets {
	uint offsets[];
};

layout(buffer_reference, scalar) buffer ClusterCursors {
	uint cursors[];
};

layout(buffer_reference, scalar) buffer ClusterLightIDs {
	uint lightIDs[];
};

// One per tile: (minSlice, maxSlice)
layout(buffer_reference, scalar) buffer ClusterTileSliceRanges {
	uvec2 ranges[];
};

// Scratch for scan
layout(buffer_reference, scalar) buffer ClusterScanScratch {
	uint scratch[];
};

layout(buffer_reference, scalar) readonly buffer LightBuffer {
	LocalLight lights[];
};

#endif

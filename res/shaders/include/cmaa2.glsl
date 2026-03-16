#ifndef CMAA2_GLSL
#define CMAA2_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

const uint CMAA2_HEAD_OFFSETXY_SHIFT = 30u;
const uint CMAA2_HEAD_OFFSETXY_MASK  = 0x3u;
const uint CMAA2_HEAD_INDEX_MASK     = 0x0FFFFFFFu;
const uint CMAA2_EMPTY_HEAD          = 0x7FFFFFFFu;
const uint CMAA2_HEAD_COMPLEX_SHIFT  = 29u;

// CMAA2 control slots
const uint CMAA2_CTRL_SHAPE_CANDIDATE_COUNT     = 0u;
const uint CMAA2_CTRL_DEFERRED_LOCATION_COUNT   = 1u;
const uint CMAA2_CTRL_DEFERRED_ITEM_COUNT       = 2u;

const uint CMAA2_PROCESS_CANDIDATES_NUM_THREADS = 128u;
const uint CMAA2_DEFERRED_APPLY_NUM_THREADS     = 32u;

const ivec2 pixelOffsets[4] = ivec2[4](
	ivec2(0, 0),
	ivec2(1, 0),
	ivec2(0, 1),
	ivec2(1, 1)
);

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

uint loadEdgeNibble(ivec2 pixelPos, usampler2D workingEdges)
{
	ivec2 edgeTexelPos = ivec2(pixelPos.x >> 1, pixelPos.y);
	uint packedByte = texelFetch(workingEdges, edgeTexelPos, 0).r;

	uint nibbleShift = (uint(pixelPos.x) & 1u) * 4u;
	return (packedByte >> nibbleShift) & 0xFu;
}

#endif

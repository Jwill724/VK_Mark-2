//#ifndef CMAA2_GLSL
//#define CMAA2_GLSL
//
//const uint CMAA2_HEAD_OFFSETXY_SHIFT = 30u;
//const uint CMAA2_HEAD_OFFSETXY_MASK  = 0x3u;
//const uint CMAA2_HEAD_INDEX_MASK     = 0x0FFFFFFFu;
//const uint CMAA2_EMPTY_HEAD          = 0x7FFFFFFFu;
//const uint CMAA2_HEAD_COMPLEX_SHIFT  = 29u;
//
//// CMAA2 control slots
//const uint CMAA2_CTRL_SHAPE_CANDIDATE_COUNT     = 0u;
//const uint CMAA2_CTRL_DEFERRED_LOCATION_COUNT   = 1u;
//const uint CMAA2_CTRL_DEFERRED_ITEM_COUNT       = 2u;
//
//const uint CMAA2_PROCESS_CANDIDATES_NUM_THREADS = 128u;
//const uint CMAA2_DEFERRED_APPLY_NUM_THREADS     = 32u;
//
//const ivec2 pixelOffsets[4] = ivec2[4](
//	ivec2(0, 0),
//	ivec2(1, 0),
//	ivec2(0, 1),
//	ivec2(1, 1)
//);
//
//uint loadEdgeNibble(ivec2 pixelPos, usampler2D workingEdges)
//{
//	ivec2 edgeTexelPos = ivec2(pixelPos.x >> 1, pixelPos.y);
//	uint packedByte = texelFetch(workingEdges, edgeTexelPos, 0).r;
//
//	uint nibbleShift = (uint(pixelPos.x) & 1u) * 4u;
//	return (packedByte >> nibbleShift) & 0xFu;
//}
//
////const uint INDIRECT_DISPATCH_SLOT_CMAA2_SHAPES         = 12u;
////const uint INDIRECT_DISPATCH_SLOT_CMAA2_DEFERRED       = 13u;
//
////const uint ABT_Cmaa2Control           = 43u;
////const uint ABT_Cmaa2ShapeCandidates   = 44u;
////const uint ABT_Cmaa2DeferredLocations = 45u;
////const uint ABT_Cmaa2DeferredItems     = 46u;
////const uint ABT_Cmaa2DeferredHeads     = 47u;
//
//
////layout(buffer_reference, scalar) buffer Cmaa2ControlBuffer {
////	uint control[];
////};
////Cmaa2ControlBuffer getCMAA2ControlBuffer() {
////	uint64_t addr = getABTFrameAddress(ABT_Cmaa2Control);
////	return Cmaa2ControlBuffer(addr);
////}
////
////layout(buffer_reference, scalar) buffer Cmaa2ShapeCandidatesBuffer {
////	uint pixelIDs[];
////};
////Cmaa2ShapeCandidatesBuffer getCMAA2ShapeCandidatesBuffer() {
////	uint64_t addr = getABTFrameAddress(ABT_Cmaa2ShapeCandidates);
////	return Cmaa2ShapeCandidatesBuffer(addr);
////}
////
////layout(buffer_reference, scalar) buffer Cmaa2DeferredLocationsBuffer {
////	uint quadIDs[];
////};
////Cmaa2DeferredLocationsBuffer getCMAA2DeferredLocationsBuffer() {
////	uint64_t addr = getABTFrameAddress(ABT_Cmaa2DeferredLocations);
////	return Cmaa2DeferredLocationsBuffer(addr);
////}
////
////layout(buffer_reference, scalar) buffer Cmaa2DeferredItemsBuffer {
////	uvec4 items[];
////};
////Cmaa2DeferredItemsBuffer getCMAA2DeferredItemsBuffer() {
////	uint64_t addr = getABTFrameAddress(ABT_Cmaa2DeferredItems);
////	return Cmaa2DeferredItemsBuffer(addr);
////}
////
////layout(buffer_reference, scalar) buffer Cmaa2DeferredHeadsBuffer {
////	uint heads[];
////};
////Cmaa2DeferredHeadsBuffer getCMAA2DeferredHeadsBuffer() {
////	uint64_t addr = getABTFrameAddress(ABT_Cmaa2DeferredHeads);
////	return Cmaa2DeferredHeadsBuffer(addr);
////}
//
//#endif
//

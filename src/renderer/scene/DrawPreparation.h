#pragma once

#include "Core.h"
#include <vector>

#include "renderer/RendererDefinitions.h"
namespace RD = RendererDefinitions;

class FrameContext;
struct LocalLight;
class Device;
class Allocator;
struct Mesh;
struct MeshLODs;
struct AABB;
struct DrawBuildOutput;
struct Instance;

namespace DrawPreparation
{
	DrawBuildOutput BuildAndSortIndirectDraws(
		const std::vector<Instance>&                                      inputVisible,
		const std::vector<AABB>&                                          worldAABBs,
		const std::vector<Mesh>&                                          meshes,
		const std::vector<MeshLODs>&                                      meshLods,
		const glm::vec4&                                                  cameraPos,
		const glm::mat4&                                                  cameraProj,
		const std::array<std::vector<Instance>, RD::MAX_SHADOW_CASCADES>& csmCasters,
		const std::vector<Instance>&                                      flashlightCasters,
		const std::vector<uint32_t>&                                      materialFlags,
		bool                                                              shadowsEnabled,
		bool                                                              flashlightOn);

	// Uploads visible instances, indirect draws, transforms, lights, and
	// per-frame address table to GPU via transfer queue.
	// Uses FrameStaging for instances/draws, GlobalStaging for transforms/lights.
	void UploadGPUBuffersForFrame(
		FrameContext&                    frameCtx,
		Device&                          device,
		Allocator&                       allocator,
		const std::vector<glm::mat4>&    transforms,
		const std::vector<LocalLight>&   lights,
		bool                             isTemporalValid);
}

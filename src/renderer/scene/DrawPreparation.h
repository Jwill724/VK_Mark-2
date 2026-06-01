#pragma once

#include "Core.h"
#include <Vector>

class FrameContext;
struct LocalLight;
class Device;
class Allocator;
struct Mesh;
struct MeshLODs;
struct AABB;
struct RenderToggles;

namespace DrawPreparation
{
	void uploadGPUBuffersForFrame(
		FrameContext& frameCtx,
		Device& device,
		Allocator& allocator,
		const std::vector<glm::mat4>& transforms,
		const std::vector<LocalLight>& lights);

	void buildAndSortIndirectDraws(
		FrameContext& frameCtx,
		const std::vector<Mesh>& meshes,
		const std::vector<MeshLODs>& meshLods,
		const std::vector<AABB>& worldAABBs,
		const glm::vec4& cameraPos,
		const glm::mat4& cameraProj,
		const RenderToggles& dbg);
}

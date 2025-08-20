#pragma once

#include "common/ResourceTypes.h"
#include "common/EngineTypes.h"

namespace MeshLoader {
	void uploadMeshes(
		ThreadContext& threadCtx,
		const std::vector<Vertex>& vertices,
		const std::vector<uint32_t>& indices,
		const MeshRegistry& meshes,
		const VmaAllocator allocator,
		const VkDevice device
	);
}
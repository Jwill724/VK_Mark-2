#pragma once

#include "common/EngineTypes.h"

namespace SceneGraph {
	void buildSceneGraph(ThreadContext& threadCtx, std::vector<GPUMeshData>& meshes);
}
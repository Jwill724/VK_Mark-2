#pragma once

#include "core/AssetManager.h"
#include "Renderer.h"

static unsigned int currentSceneIndex = 1;

namespace RenderScene {
	GPUSceneData& getCurrentSceneData();

	void setMeshes(std::vector<std::shared_ptr<MeshAsset>>& meshes);

	inline std::vector<std::shared_ptr<MeshAsset>> _sceneMeshes;

//	extern VkDescriptorSetLayout _singleImageDescriptorLayout;

	VkDescriptorSetLayout& getGPUSceneDescriptorLayout();
	VkDescriptorSetLayout& getSingleImageDescriptorLayout();
	void transformMesh(GPUDrawPushConstants& pushConstants, float aspect);
	void renderDrawScene(VkCommandBuffer cmd, FrameData& frame);
}
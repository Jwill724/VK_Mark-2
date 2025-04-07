#pragma once

#include "core/AssetManager.h"
#include "Renderer.h"
#include "vulkan/PipelineManager.h"
#include "input/Camera.h"

static unsigned int currentSceneIndex = 1;

struct GLTFMetallic_Roughness {
	MaterialPipeline opaquePipeline;
	MaterialPipeline transparentPipeline;

	VkDescriptorSetLayout materialLayout;

	struct MaterialConstants {
		glm::vec4 colorFactors;
		glm::vec4 metal_rough_factors;
		glm::vec4 extra[14];
	};

	struct MaterialResources {
		AllocatedImage colorImage;
		VkSampler colorSampler;
		AllocatedImage metalRoughImage;
		VkSampler metalRoughSampler;
		VkBuffer dataBuffer;
		uint32_t dataBufferOffset;
	};

	DescriptorWriter writer;

	void buildPipelines(PipelineConfigPresent& pipelineInfo);
	void clearResources(VkDevice device);

	MaterialInstance writeMaterial(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorManager& descriptor);
};

// Holds and controls scene data
namespace RenderScene {
	GPUSceneData& getCurrentSceneData();

	extern Camera mainCamera;

	extern DrawContext mainDrawContext;
	extern GLTFMetallic_Roughness metalRoughMaterial;

	void setMeshes(std::vector<std::shared_ptr<MeshAsset>>& meshes);

	inline std::vector<std::shared_ptr<MeshAsset>> _sceneMeshes;

	VkDescriptorSetLayout& getGPUSceneDescriptorLayout();
	VkDescriptorSetLayout& getSingleImageDescriptorLayout();
	void updateScene();
	void renderDrawScene(VkCommandBuffer cmd, FrameData& frame);
	void createSceneData();
}
#pragma once

#include "core/AssetManager.h"
#include "Renderer.h"
#include "vulkan/PipelineManager.h"
#include "input/Camera.h"

constexpr glm::vec3 SPAWNPOINT(1, 1, -1);

struct GLTFMetallic_Roughness {
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

	void clearResources(VkDevice device);

	MaterialInstance writeMaterial(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorManager& descriptor);
};

// Holds and controls scene data
namespace RenderScene {
	GPUSceneData& getCurrentSceneData();

	extern std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> loadedScenes;

	extern Camera mainCamera;

	extern DrawContext mainDrawContext;
	extern GLTFMetallic_Roughness metalRoughMaterial;

	void setCamera();

	inline std::vector<std::shared_ptr<MeshAsset>> _sceneMeshes;

	VkDescriptorSetLayout& getGPUSceneDescriptorLayout();

	void updateScene();
	void renderDrawScene(VkCommandBuffer cmd, FrameData& frame);
	void createSceneData();
}
#include "pch.h"

#include "common/Vk_Types.h"
#include "RenderScene.h"
#include "Renderer.h"
#include "vulkan/Backend.h"
#include "vulkan/PipelineManager.h"

namespace RenderScene {
	GPUSceneData _sceneData;
	GPUSceneData& getCurrentSceneData() { return _sceneData; }

	VkDescriptorSetLayout _gpuSceneDataDescriptorLayout;
	VkDescriptorSetLayout _singleImageDescriptorLayout;
	VkDescriptorSetLayout& getGPUSceneDescriptorLayout() { return _gpuSceneDataDescriptorLayout; }
	VkDescriptorSetLayout& getSingleImageDescriptorLayout() { return _singleImageDescriptorLayout; }

	void renderDrawScene(VkCommandBuffer cmd, FrameData& frame);
	void updateScene(VkCommandBuffer cmd, FrameData& frame);
	void drawSceneMeshes(VkCommandBuffer cmd);
}

void RenderScene::renderDrawScene(VkCommandBuffer cmd, FrameData& frame) {
	updateScene(cmd, frame);
}

void RenderScene::setMeshes(std::vector<std::shared_ptr<MeshAsset>>& meshes) {
	_sceneMeshes = meshes;
}

void RenderScene::updateScene(VkCommandBuffer cmd, FrameData& frame) {

	VkDescriptorSet sceneDescriptor = frame._frameDescriptors.allocateDescriptor(_gpuSceneDataDescriptorLayout);

	DescriptorWriter writer;
	writer.writeBuffer(0, RenderScene::_sceneMeshes[currentSceneIndex]->meshBuffers.vertexBuffer.buffer,
		RenderScene::_sceneMeshes[currentSceneIndex]->meshBuffers.vertexBuffer.info.size, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

	writer.writeImage(1, AssetManager::getCheckboardTex().imageView, AssetManager::getDefaultSamplerNearest(),
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

	writer.updateSet(Backend::getDevice(), sceneDescriptor);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipelines::meshPipeline.getPipelineLayout(), 0, 1, &sceneDescriptor, 0, nullptr);
}

void RenderScene::transformMesh(GPUDrawPushConstants& pushConstants, float aspect) {
	glm::mat4 view = glm::translate(glm::vec3{ 0, 0, -5 });
	// camera projection
	glm::mat4 projection = glm::perspective(glm::radians(40.f), aspect, 1.f, 10.f);

	// invert the Y direction on projection matrix so that we are more similar
	// to opengl and gltf axis
	projection[1][1] *= -1;

	pushConstants.worldMatrix = projection * view;
	pushConstants.vertexBuffer = _sceneMeshes[currentSceneIndex]->meshBuffers.vertexBufferAddress;
}

void RenderScene::drawSceneMeshes(VkCommandBuffer cmd) {}
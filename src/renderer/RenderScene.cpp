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
//	VkDescriptorSetLayout _singleImageDescriptorLayout;
	VkDescriptorSetLayout& getGPUSceneDescriptorLayout() { return _gpuSceneDataDescriptorLayout; }
//	VkDescriptorSetLayout& getSingleImageDescriptorLayout() { return _singleImageDescriptorLayout; }

	void renderDrawScene(VkCommandBuffer cmd, FrameData& frame);
	void updateGlobalSceneUniforms(VkCommandBuffer cmd, FrameData& frame);
	void drawSceneMeshes(VkCommandBuffer cmd);
}

void RenderScene::renderDrawScene(VkCommandBuffer cmd, FrameData& frame) {
	updateGlobalSceneUniforms(cmd, frame);
//	drawSceneMeshes(cmd);
}

void RenderScene::setMeshes(std::vector<std::shared_ptr<MeshAsset>>& meshes) {
	_sceneMeshes = meshes;
}

void RenderScene::updateGlobalSceneUniforms(VkCommandBuffer cmd, FrameData& frame) {
	//add it to the deletion queue of this frame so it gets deleted once its been used

	//allocate a new uniform buffer for the scene data
	AllocatedBuffer gpuSceneDataBuffer = BufferUtils::createBuffer(sizeof(GPUSceneData),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, Renderer::getRenderImageAllocator());
//	std::cout << "[updateGlobalSceneUniforms] Allocated GPUSceneData buffer: " << gpuSceneDataBuffer.buffer << std::endl;

	frame._deletionQueue.push_function([=]() {
		BufferUtils::destroyBuffer(gpuSceneDataBuffer, Renderer::getRenderImageAllocator());
	//	std::cout << "[updateGlobalSceneUniforms] Deleting GPUSceneData buffer: " << gpuSceneDataBuffer.buffer << std::endl;
	});

	//write the buffer
	GPUSceneData* sceneUniformData = reinterpret_cast<GPUSceneData*>(gpuSceneDataBuffer.info.pMappedData);
	*sceneUniformData = _sceneData;
	//std::cout << "[updateGlobalSceneUniforms] Written scene data to buffer at address: " << gpuSceneDataBuffer.info.pMappedData << std::endl;

	//create a descriptor set that binds that buffer and update it
	VkDescriptorSet globalDescriptor = frame._frameDescriptors.allocateDescriptor(_gpuSceneDataDescriptorLayout);
	//std::cout << "[updateGlobalSceneUniforms] Allocated descriptor set: " << globalDescriptor << std::endl;

	DescriptorWriter writer;
	writer.writeBuffer(0, RenderScene::_sceneMeshes[2]->meshBuffers.vertexBuffer.buffer,
		RenderScene::_sceneMeshes[2]->meshBuffers.vertexBuffer.info.size, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

//	writer.writeBuffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

	writer.updateSet(Backend::getDevice(), globalDescriptor);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipelines::meshPipeline.getPipelineLayout(), 0, 1, &globalDescriptor, 0, nullptr);
}

void RenderScene::transformMesh(GPUDrawPushConstants& pushConstants, float aspect) {
	glm::mat4 view = glm::translate(glm::vec3{ 0, 0, -4 });
	// camera projection
	glm::mat4 projection = glm::perspective(glm::radians(50.f), aspect, 1.f, 10.f);

	// invert the Y direction on projection matrix so that we are more similar
	// to opengl and gltf axis
	projection[1][1] *= -1;

	pushConstants.worldMatrix = projection * view;
	pushConstants.vertexBuffer = _sceneMeshes[2]->meshBuffers.vertexBufferAddress;
}

void RenderScene::drawSceneMeshes(VkCommandBuffer cmd) {}
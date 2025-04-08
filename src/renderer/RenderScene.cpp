#include "pch.h"

#include "common/Vk_Types.h"
#include "RenderScene.h"
#include "Renderer.h"
#include "vulkan/Backend.h"
#include "vulkan/PipelineManager.h"
#include "SceneGraph.h"

namespace RenderScene {
	GPUSceneData sceneData;
	GPUSceneData& getCurrentSceneData() { return sceneData; }

	Camera mainCamera;

	DrawContext mainDrawContext;
	std::unordered_map<std::string, std::shared_ptr<Node>> loadedNodes;

	MaterialInstance _defaultData;
	GLTFMetallic_Roughness metalRoughMaterial;

	VkDescriptorSetLayout _gpuSceneDataDescriptorLayout;
	VkDescriptorSetLayout _singleImageDescriptorLayout;
	VkDescriptorSetLayout& getGPUSceneDescriptorLayout() { return _gpuSceneDataDescriptorLayout; }
	VkDescriptorSetLayout& getSingleImageDescriptorLayout() { return _singleImageDescriptorLayout; }
}

void RenderScene::renderDrawScene(VkCommandBuffer cmd, FrameData& frame) {
	VkDevice device = Backend::getDevice();

	AllocatedBuffer gpuSceneDataBuffer = BufferUtils::createBuffer(sizeof(GPUSceneData),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, Renderer::getRenderImageAllocator());

	//add it to the deletion queue of this frame so it gets deleted once its been used
	frame._deletionQueue.push_function([=]() {
		BufferUtils::destroyBuffer(gpuSceneDataBuffer, Renderer::getRenderImageAllocator());
	});

	//write the buffer
	GPUSceneData* sceneDataPtr = reinterpret_cast<GPUSceneData*>(gpuSceneDataBuffer.mapped);
	*reinterpret_cast<GPUSceneData*>(sceneDataPtr) = sceneData;

	//create a descriptor set that binds that buffer and update it
	VkDescriptorSet sceneDescriptor = frame._frameDescriptors.allocateDescriptor(device, _gpuSceneDataDescriptorLayout);

	DescriptorWriter writer;
	writer.writeBuffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	writer.updateSet(device, sceneDescriptor);

	PushConstantDef matsPipePC = Pipelines::metalRoughMatConfigs.pushConstantsInfo;

	// Will fix later, inefficient to do all these draws
	for (const RenderObject& draw : mainDrawContext.OpaqueSurfaces) {

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.material->pipeline->pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.material->pipeline->pipelineLayout, 0, 1, &sceneDescriptor, 0, nullptr);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.material->pipeline->pipelineLayout, 1, 1, &draw.material->materialSet, 0, nullptr);

		vkCmdBindIndexBuffer(cmd, draw.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

		GPUDrawPushConstants pushConstants;
		pushConstants.vertexBuffer = draw.vertexBufferAddress;
		pushConstants.worldMatrix = draw.transform;

		vkCmdPushConstants(cmd, draw.material->pipeline->pipelineLayout, matsPipePC.stageFlags, matsPipePC.offset, matsPipePC.size, &pushConstants);

		vkCmdDrawIndexed(cmd, draw.indexCount, 1, draw.firstIndex, 0, 0);
	}
}

void RenderScene::setMeshes(std::vector<std::shared_ptr<MeshAsset>>& meshes) {
	for (auto& m : meshes) {
		std::shared_ptr<MeshNode> newNode = std::make_shared<MeshNode>();
		newNode->mesh = m;

		newNode->localTransform = glm::mat4{ 1.f };
		newNode->worldTransform = glm::mat4{ 1.f };

		for (auto& s : newNode->mesh->surfaces) {
			s.material = std::make_shared<GLTFMaterial>(_defaultData);
		}

		loadedNodes[m->name] = std::move(newNode);
	}

	// CAMERA SETUP IN SET MESHES TEMP
	mainCamera.velocity = glm::vec3(0.f);
	mainCamera.position = glm::vec3(0, 0, 5);

	mainCamera.pitch = 0;
	mainCamera.yaw = -90.f;
}

// Mesh transforms
void RenderScene::updateScene() {

	float aspect = static_cast<float>(Renderer::getDrawExtent().width) / static_cast<float>(Renderer::getDrawExtent().height);

	mainDrawContext.OpaqueSurfaces.clear();

	//for (auto& m : loadedNodes) {
	//	m.second->Draw(glm::mat4{ 1.f }, mainDrawContext);
	//}

	mainCamera.update(Engine::getWindow(), Engine::getLastTimeCount());

	glm::mat4 view = mainCamera.getViewMatrix();

	// camera projection
	glm::mat4 projection = glm::perspective(glm::radians(45.f), aspect, 0.1f, 1000.f);

	// invert the Y direction on projection matrix so that we are more similar
	// to opengl and gltf axis
	projection[1][1] *= -1;

	sceneData.view = view;
	sceneData.proj = projection;
	sceneData.viewproj = projection * view;

	sceneData.ambientColor = glm::vec4(.1f);
	sceneData.sunlightColor = glm::vec4(1.f);
	sceneData.sunlightDirection = glm::vec4(0, 1, 0.5, 1.f);

	for (int x = -3; x < 3; x++) {

		glm::mat4 scale = glm::scale(glm::vec3{ 0.2f });
		glm::mat4 translation = glm::translate(glm::vec3{ x, 1, 0 });

		loadedNodes["Cube"]->Draw(translation * scale, mainDrawContext);
	}

	loadedNodes["Suzanne"]->Draw(glm::mat4{ 1.f }, mainDrawContext);
}

// setups descriptors, push constants, shaders too
void GLTFMetallic_Roughness::buildPipelines(PipelineConfigPresent& pipelineInfo) {
	DeletionQueue shaderDeletionQ;

	// shader setup
	std::vector<ShaderStageInfo> stageInfo;
	ShaderStageInfo vertexStage = {
		.filePath = "res/shaders/mesh_vert.spv",
		.stage = VK_SHADER_STAGE_VERTEX_BIT
	};
	ShaderStageInfo fragmentStage = {
		.filePath = "res/shaders/mesh_frag.spv",
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT
	};
	stageInfo.push_back(vertexStage);
	stageInfo.push_back(fragmentStage);

	PipelineManager::setupShaders(pipelineInfo.shaderStages, stageInfo, shaderDeletionQ);

	// descriptors setup
	DescriptorSetOverwatch::descriptorManager.clearBinding();

	DescriptorSetOverwatch::descriptorManager.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
	DescriptorSetOverwatch::descriptorManager.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
	DescriptorSetOverwatch::descriptorManager.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);

	materialLayout = DescriptorSetOverwatch::descriptorManager.createSetLayout(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

	pipelineInfo.descriptorSetInfo.descriptorLayouts = { RenderScene::getGPUSceneDescriptorLayout(), materialLayout };

	// pipeline layout handles default push constant data for meshes
	// both pipelines will share the same layout data
	VkPipelineLayout newLayout = PipelineManager::setupPipelineLayout(pipelineInfo);

	opaquePipeline.pipelineLayout = newLayout;
	transparentPipeline.pipelineLayout = newLayout;

	// pipeline stage configs
	PipelineBuilder pipeline_builder;
	pipelineInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	pipelineInfo.polygonMode = VK_POLYGON_MODE_FILL;
	pipelineInfo.enableBackfaceCulling = true;
	pipelineInfo.enableBlending = false;
	pipelineInfo.enableDepthTest = true;
	pipelineInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	pipelineInfo.colorFormat = Renderer::getDrawImage().imageFormat;
	pipelineInfo.depthFormat = Renderer::getDepthImage().imageFormat;

	pipeline_builder._pipelineLayout = newLayout;

	PipelineManager::setupPipelineConfig(pipeline_builder, pipelineInfo);

	opaquePipeline.pipeline = pipeline_builder.createPipeline(pipelineInfo);

	if (opaquePipeline.pipeline == VK_NULL_HANDLE) {
		std::cout << "Error: Opaque pipeline was not created properly.\n";
	}
	else {
		std::cout << "Opaque pipeline built: " << opaquePipeline.pipeline << "\n";
	}

	// transparent pipeline
	pipelineInfo.enableBlending = true;
	pipelineInfo.enableDepthTest = false;
	pipelineInfo.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;

	pipeline_builder.initializePipelineSTypes();
	PipelineManager::setupPipelineConfig(pipeline_builder, pipelineInfo);

	transparentPipeline.pipeline = pipeline_builder.createPipeline(pipelineInfo);

	if (transparentPipeline.pipeline == VK_NULL_HANDLE) {
		std::cout << "Error: Transparent pipeline was not created properly.\n";
	}
	else {
		std::cout << "Transparent pipeline built: " << transparentPipeline.pipeline << "\n";
	}

	shaderDeletionQ.flush(); // deferred deletion of shader modules
}

MaterialInstance GLTFMetallic_Roughness::writeMaterial(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorManager& descriptor) {
	MaterialInstance matData;
	matData.passType = pass;
	if (pass == MaterialPass::Transparent) {
		matData.pipeline = &transparentPipeline;
	}
	else {
		matData.pipeline = &opaquePipeline;
	}

	matData.materialSet = descriptor.allocateDescriptor(device, materialLayout);

	writer.clear();
	writer.writeBuffer(0, resources.dataBuffer, sizeof(MaterialConstants), resources.dataBufferOffset, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	writer.writeImage(1, resources.colorImage.imageView, resources.colorSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	writer.writeImage(2, resources.metalRoughImage.imageView, resources.metalRoughSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

	writer.updateSet(device, matData.materialSet);

	return matData;
}

void RenderScene::createSceneData() {
	GLTFMetallic_Roughness::MaterialResources materialResources;
	//default the material textures
	materialResources.colorImage = AssetManager::getWhiteImage();
	materialResources.colorSampler = AssetManager::getDefaultSamplerLinear();
	materialResources.metalRoughImage = AssetManager::getWhiteImage();
	materialResources.metalRoughSampler = AssetManager::getDefaultSamplerLinear();

	//set the uniform buffer for the material data
	AllocatedBuffer materialConstants = BufferUtils::createBuffer(sizeof(GLTFMetallic_Roughness::MaterialConstants),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, Renderer::getRenderImageAllocator());

	GLTFMetallic_Roughness::MaterialConstants* sceneDataPtr = reinterpret_cast<GLTFMetallic_Roughness::MaterialConstants*>(materialConstants.mapped);

	sceneDataPtr->colorFactors = glm::vec4{ 1,1,1,1 };
	sceneDataPtr->metal_rough_factors = glm::vec4{ 1,0.5,0,0 };

	Engine::getDeletionQueue().push_function([=]() {
		BufferUtils::destroyBuffer(materialConstants, Renderer::getRenderImageAllocator());
	});

	materialResources.dataBuffer = materialConstants.buffer;
	materialResources.dataBufferOffset = 0;

	_defaultData = metalRoughMaterial.writeMaterial(Backend::getDevice(), MaterialPass::MainColor,
		materialResources, DescriptorSetOverwatch::descriptorManager);
}

void GLTFMetallic_Roughness::clearResources(VkDevice device)
{
	vkDestroyDescriptorSetLayout(device, materialLayout, nullptr);
	vkDestroyPipelineLayout(device, transparentPipeline.pipelineLayout, nullptr);

	vkDestroyPipeline(device, transparentPipeline.pipeline, nullptr);
	vkDestroyPipeline(device, opaquePipeline.pipeline, nullptr);
}
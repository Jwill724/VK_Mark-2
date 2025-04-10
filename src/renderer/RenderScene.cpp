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

	std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> loadedScenes;

	MaterialInstance _defaultData;
	GLTFMetallic_Roughness metalRoughMaterial;

	VkDescriptorSetLayout _gpuSceneDataDescriptorLayout;
	VkDescriptorSetLayout& getGPUSceneDescriptorLayout() { return _gpuSceneDataDescriptorLayout; }
}

void RenderScene::setCamera() {
	mainCamera.velocity = glm::vec3(0.f);
	mainCamera.position = glm::vec3(30.f, -00.f, -085.f);

	mainCamera.pitch = 0;
	mainCamera.yaw = -90.f;
}

void RenderScene::renderDrawScene(VkCommandBuffer cmd, FrameData& frame) {
	//reset counters
	auto& stats = Engine::getStats();

	stats.drawcallCount = 0;
	stats.triangleCount = 0;

	//begin clock
	auto start = std::chrono::system_clock::now();

	VkDevice device = Backend::getDevice();

	// SORT INDICES OF DRAW ARRAY
	std::vector<uint32_t> opaque_draws;
	opaque_draws.reserve(mainDrawContext.OpaqueSurfaces.size());

	for (uint32_t i = 0; i < mainDrawContext.OpaqueSurfaces.size(); i++) {
		opaque_draws.push_back(i);
	}

	// sort the opaque surfaces by material and mesh
	std::sort(opaque_draws.begin(), opaque_draws.end(), [&](const auto& iA, const auto& iB) {
		const RenderObject& A = mainDrawContext.OpaqueSurfaces[iA];
		const RenderObject& B = mainDrawContext.OpaqueSurfaces[iB];
		if (A.material == B.material) {
			return A.indexBuffer < B.indexBuffer;
		}
		else {
			return A.material < B.material;
		}
	});

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

	// draw state tracking
	MaterialPipeline* lastPipeline = nullptr;
	MaterialInstance* lastMaterial = nullptr;
	VkBuffer lastIndexBuffer = VK_NULL_HANDLE;

	auto extent = Renderer::getDrawExtent();

	auto draw = [&](const RenderObject& r) {
		if (r.material != lastMaterial) {
			lastMaterial = r.material;
			//rebind pipeline and descriptors if the material changed
			if (r.material->pipeline != lastPipeline) {

				lastPipeline = r.material->pipeline;
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->pipeline);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->pipelineLayout, 0, 1,
					&sceneDescriptor, 0, nullptr);

				VkViewport viewport = {};
				viewport.x = 0;
				viewport.y = 0;
				viewport.width = static_cast<float>(extent.width);
				viewport.height = static_cast<float>(extent.height);
				viewport.minDepth = 0.f;
				viewport.maxDepth = 1.f;

				vkCmdSetViewport(cmd, 0, 1, &viewport);

				VkRect2D scissor = {};
				scissor.offset.x = 0;
				scissor.offset.y = 0;
				scissor.extent.width = extent.width;
				scissor.extent.height = extent.height;

				vkCmdSetScissor(cmd, 0, 1, &scissor);
			}

			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->pipelineLayout, 1, 1,
				&r.material->materialSet, 0, nullptr);
		}
		//rebind index buffer if needed
		if (r.indexBuffer != lastIndexBuffer) {
			lastIndexBuffer = r.indexBuffer;
			vkCmdBindIndexBuffer(cmd, r.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
		}
		// calculate final mesh matrix
		GPUDrawPushConstants push_constants;
		push_constants.worldMatrix = r.transform;
		push_constants.vertexBuffer = r.vertexBufferAddress;

		vkCmdPushConstants(cmd, r.material->pipeline->pipelineLayout, matsPipePC.stageFlags, matsPipePC.offset, matsPipePC.size, &push_constants);

		vkCmdDrawIndexed(cmd, r.indexCount, 1, r.firstIndex, 0, 0);
		//stats
		stats.drawcallCount++;
		stats.triangleCount += r.indexCount / 3;
	};

	for (auto& r : opaque_draws) {
		draw(mainDrawContext.OpaqueSurfaces[r]);
	}

	// need depth sort
	for (auto& r : mainDrawContext.TransparentSurfaces) {
		draw(r);
	}

	mainDrawContext.OpaqueSurfaces.clear();
	mainDrawContext.TransparentSurfaces.clear();

	auto end = std::chrono::system_clock::now();

	//convert to microseconds (integer), and then come back to miliseconds
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	stats.meshDrawTime = elapsed.count() / 1000.f;
}

// Mesh transforms
void RenderScene::updateScene() {
	//reset counters
	auto& stats = Engine::getStats();

	stats.drawcallCount = 0;
	stats.triangleCount = 0;

	//begin clock
	auto start = std::chrono::system_clock::now();

	float aspect = static_cast<float>(Renderer::getDrawExtent().width) / static_cast<float>(Renderer::getDrawExtent().height);

	mainCamera.update(Engine::getWindow(), Engine::getLastTimeCount());

	glm::mat4 view = mainCamera.getViewMatrix();

	// camera projection
	glm::mat4 projection = glm::perspective(glm::radians(80.f), aspect, 0.1f, 10000.f);

	// invert the Y direction on projection matrix so that we are more similar
	// to opengl and gltf axis
	projection[1][1] *= -1;

	sceneData.view = view;
	sceneData.proj = projection;
	sceneData.viewproj = projection * view;

	sceneData.ambientColor = glm::vec4(.1f);
	sceneData.sunlightColor = glm::vec4(1.f);
	sceneData.sunlightDirection = glm::vec4(0, 1, 0.5, 1.f);

	mainDrawContext.OpaqueSurfaces.clear(); // Clear from last frame

	mainDrawContext.sceneData = sceneData;
	loadedScenes["structure"]->Draw(glm::mat4{ 1.f }, mainDrawContext);

	auto end = std::chrono::system_clock::now();

	//convert to microseconds (integer), and then come back to miliseconds
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	stats.sceneUpdateTime = elapsed.count() / 1000.f;
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
	DescriptorSetOverwatch::imageDescriptorManager.clearBinding();

	DescriptorSetOverwatch::imageDescriptorManager.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
	DescriptorSetOverwatch::imageDescriptorManager.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
	DescriptorSetOverwatch::imageDescriptorManager.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);

	materialLayout = DescriptorSetOverwatch::imageDescriptorManager.createSetLayout(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

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

	//if (opaquePipeline.pipeline == VK_NULL_HANDLE) {
	//	std::cout << "Error: Opaque pipeline was not created properly.\n";
	//}
	//else {
	//	std::cout << "Opaque pipeline built: " << opaquePipeline.pipeline << "\n";
	//}

	// transparent pipeline
	pipelineInfo.enableBlending = true;
	pipelineInfo.enableDepthTest = false;
	pipelineInfo.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;

	pipeline_builder.initializePipelineSTypes();
	PipelineManager::setupPipelineConfig(pipeline_builder, pipelineInfo);

	transparentPipeline.pipeline = pipeline_builder.createPipeline(pipelineInfo);

	//if (transparentPipeline.pipeline == VK_NULL_HANDLE) {
	//	std::cout << "Error: Transparent pipeline was not created properly.\n";
	//}
	//else {
	//	std::cout << "Transparent pipeline built: " << transparentPipeline.pipeline << "\n";
	//}

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
		materialResources, DescriptorSetOverwatch::imageDescriptorManager);
}

void GLTFMetallic_Roughness::clearResources(VkDevice device) {
	vkDestroyDescriptorSetLayout(device, materialLayout, nullptr);
	vkDestroyPipelineLayout(device, transparentPipeline.pipelineLayout, nullptr);

	vkDestroyPipeline(device, transparentPipeline.pipeline, nullptr);
	vkDestroyPipeline(device, opaquePipeline.pipeline, nullptr);
}
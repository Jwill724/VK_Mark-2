#include "pch.h"

#include "Environment.h"
#include "engine/Engine.h"
#include "AssetManager.h"
#include "renderer/Renderer.h"

struct alignas(16) SpecularPC {
	float roughness;
	uint32_t width;
	uint32_t height;
	uint32_t sampleCount;
	uint32_t skyboxViewIdx;
	uint32_t specularStorageIdx;
	uint32_t pad[2];
};

namespace Environment {
	void dispatchPrefilterEnvmap(VkCommandBuffer cmd,
		std::vector<SpecularPC> pushConstants,
		PipelineObj& pipeline,
		PipelineLayoutConst layout);
	void dispatchDiffuseIrradiance(VkCommandBuffer cmd,
		ImageLUTEntry entry, PipelineObj& pipeline,
		PipelineLayoutConst layout);
	void dispatchHDRToCubemap(VkCommandBuffer cmd,
		ImageLUTEntry entry, PipelineObj& pipeline,
		PipelineLayoutConst layout);
	void dispatchBRDFLUT(VkCommandBuffer cmd,
		ImageLUTEntry entry,
		PipelineObj& pipeline,
		PipelineLayoutConst layout);

	AllocatedImage loadHDR(
		const char* hdrPath,
		VkCommandPool cmdPool,
		DeletionQueue& imageQueue,
		DeletionQueue& bufferQueue,
		const VmaAllocator allocator,
		const VkDevice device);
}

AllocatedImage Environment::loadHDR(
	const char* hdrPath,
	VkCommandPool cmdPool,
	DeletionQueue& imageQueue,
	DeletionQueue& bufferQueue,
	const VmaAllocator allocator,
	const VkDevice device)
{

	int w, h, channels;
	float* hdrData = stbi_loadf(hdrPath, &w, &h, &channels, 4);

	if (!hdrData) {
		fmt::print("Failed to load HDR: {}\n", stbi_failure_reason());
		ASSERT(true);
	}

	AllocatedImage equirect{};
	equirect.imageExtent = { uint32_t(w), uint32_t(h), 1 };
	equirect.imageFormat = VK_FORMAT_R32G32B32A32_SFLOAT;

	ImageUtils::createTextureImage(
		device,
		cmdPool,
		hdrData,
		equirect,
		VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		imageQueue,
		bufferQueue,
		allocator);

	stbi_image_free(hdrData);

	return equirect;
}

void Environment::dispatchEnvironmentMaps(
	const VkDevice device,
	GPUResources& resources,
	ImageTableManager& globalImgTable)
{
	//AllocatedImage equirect = loadHDR("res/assets/envhdr/kloppenheim_06_puresky_4k.hdr",
	// resources.getGraphicsPool(),
	// resources.getTempDeletionQueue(),
	// resources.getTempDeletionQueue(),
	// resources.getAllocator()
	// device);
	AllocatedImage equirect = loadHDR("res/assets/envhdr/meadow_4k.hdr",
		resources.getGraphicsPool(),
		resources.getTempDeletionQueue(),
		resources.getTempDeletionQueue(),
		resources.getAllocator(),
		device);
	//AllocatedImage equirect = loadHDR("res/assets/envhdr/wasteland_clouds_4k.hdr",
	// resources.getGraphicsPool(),
	// resources.getTempDeletionQueue(),
	// resources.getTempDeletionQueue(),
	// resources.getAllocator()
	// device);
	//AllocatedImage equirect = loadHDR("res/assets/envhdr/rogland_clear_night_4k.hdr",
	// resources.getGraphicsPool(),
	// resources.getTempDeletionQueue(),
	// resources.getTempDeletionQueue(),
	// resources.getAllocator()
	// device);

	auto& skyboxImg = ResourceManager::getSkyBoxImage();
	auto& skyboxSmpl = ResourceManager::getSkyBoxSampler();

	auto& diffuseImg = ResourceManager::getIrradianceImage();
	auto& diffuseSmpl = ResourceManager::getIrradianceSampler();

	auto& specImg = ResourceManager::getSpecularPrefilterImage();
	auto& specSmpl = ResourceManager::getSpecularPrefilterSampler();

	auto& brdfImg = ResourceManager::getBRDFImage();

	ImageLUTEntry tempEntryEquirect{};
	ImageLUTEntry tempEntryDiffuse{};
	ImageLUTEntry tempEntryBRDF{};

	tempEntryEquirect.combinedImageIndex = globalImgTable.addCombinedImage(equirect.imageView, skyboxSmpl);
	tempEntryEquirect.storageImageIndex = globalImgTable.addStorageImage(skyboxImg.storageView);
	resources.addImageLUTEntry(tempEntryEquirect);

	tempEntryBRDF.storageImageIndex = globalImgTable.addStorageImage(brdfImg.storageView);
	resources.addImageLUTEntry(tempEntryBRDF);

	tempEntryDiffuse.samplerCubeIndex = globalImgTable.addCubeImage(skyboxImg.imageView, diffuseSmpl);
	tempEntryDiffuse.storageImageIndex = globalImgTable.addStorageImage(diffuseImg.storageView);
	resources.addImageLUTEntry(tempEntryDiffuse);

	// Storage view defined per mip level

	uint32_t skyboxIdx = globalImgTable.addCubeImage(skyboxImg.imageView, specSmpl);
	const uint32_t specMipLevels = specImg.mipLevelCount;

	std::vector<SpecularPC> specularPushConstants;
	for (uint32_t mip = 0; mip < specMipLevels; ++mip) {
		float roughness = float(mip) / float(specMipLevels - 1);

		ImageLUTEntry tempEntrySpecular{};
		tempEntrySpecular.samplerCubeIndex = skyboxIdx;

		VkImageView mipView = specImg.storageViews[mip];
		uint32_t storageIdx = globalImgTable.addStorageImage(mipView);
		tempEntrySpecular.storageImageIndex = storageIdx;

		resources.addImageLUTEntry(tempEntrySpecular);

		SpecularPC pc{};
		pc.skyboxViewIdx = tempEntrySpecular.samplerCubeIndex;
		pc.specularStorageIdx = tempEntrySpecular.storageImageIndex;

		pc.sampleCount = PREFILTER_SAMPLE_COUNT;
		pc.roughness = roughness;
		pc.width = std::max(1u, specImg.imageExtent.width >> mip);
		pc.height = std::max(1u, specImg.imageExtent.height >> mip);

		fmt::print("\n--- Mip {} Prefilter Setup ---\n", mip);
		fmt::print("-> Roughness: {:.3f}\n", roughness);
		fmt::print("-> Skybox Sampler Index: {}\n", skyboxIdx);
		fmt::print("-> Storage View Handle: {}\n", static_cast<void*>(mipView));
		fmt::print("-> Pushed Storage Index: {}\n", storageIdx);
		fmt::print("-> PushConstant: skyboxViewIdx = {}, storageIdx = {}\n", pc.skyboxViewIdx, pc.specularStorageIdx);
		fmt::print("-> Dispatch Dimensions: {}x{} (SampleCount: {})\n", pc.width, pc.height, pc.sampleCount);

		specularPushConstants.push_back(pc);
	}

	auto& graphicsPool = resources.getGraphicsPool();
	CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {
		ImageUtils::transitionImage(cmd,
			equirect.image,
			equirect.imageFormat,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		ImageUtils::transitionImage(cmd,
			skyboxImg.image,
			skyboxImg.imageFormat,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL);
		ImageUtils::transitionImage(cmd,
			specImg.image,
			specImg.imageFormat,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL);
		ImageUtils::transitionImage(cmd,
			diffuseImg.image,
			diffuseImg.imageFormat,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL);
		ImageUtils::transitionImage(cmd,
			brdfImg.image,
			brdfImg.imageFormat,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL);
	}, graphicsPool, QueueType::Graphics, device);

	auto& graphicsQ = Backend::getGraphicsQueue();

	resources.getLastSubmittedFence() = Engine::getState().submitCommandBuffers(graphicsQ);

	waitAndRecycleLastFence(resources.getLastSubmittedFence(), graphicsQ, device);

	auto set = DescriptorSetOverwatch::getUnifiedDescriptors().descriptorSet;
	DescriptorWriter writer;
	writer.writeFromImageLUT(resources.getLUTManager().getEntries(), globalImgTable.table);
	writer.writeImages(GLOBAL_BINDING_SAMPLER_CUBE, DescriptorImageType::SamplerCube, set);
	writer.writeImages(GLOBAL_BINDING_STORAGE_IMAGE, DescriptorImageType::StorageImage, set);
	writer.writeImages(GLOBAL_BINDING_COMBINED_SAMPLER, DescriptorImageType::CombinedSampler, set);
	writer.updateSet(device, set);

	CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {
		// Bind once
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, Pipelines::_globalLayout.layout, GLOBAL_SET, 1, &set, 0, nullptr);

		dispatchHDRToCubemap(cmd, tempEntryEquirect, Pipelines::hdr2cubemapPipeline, Pipelines::_globalLayout);
		ImageUtils::transitionImage(cmd,
			skyboxImg.image,
			skyboxImg.imageFormat,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		ImageUtils::generateCubemapMiplevels(cmd, skyboxImg);


		// DIFFUSE IRRADIANCE
		dispatchDiffuseIrradiance(cmd, tempEntryDiffuse, Pipelines::diffuseIrradiancePipeline, Pipelines::_globalLayout);
		ImageUtils::transitionImage(cmd,
			diffuseImg.image,
			diffuseImg.imageFormat,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);


		// SPECULAR PREFILTER
		dispatchPrefilterEnvmap(cmd, specularPushConstants, Pipelines::specularPrefilterPipeline, Pipelines::_globalLayout);
		ImageUtils::transitionImage(cmd,
			specImg.image,
			specImg.imageFormat,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);


		// BRDF
		dispatchBRDFLUT(cmd, tempEntryBRDF, Pipelines::brdfLutPipeline, Pipelines::_globalLayout);
		ImageUtils::transitionImage(cmd,
			brdfImg.image,
			brdfImg.imageFormat,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	}, graphicsPool, QueueType::Graphics, device);

	resources.getLastSubmittedFence() = Engine::getState().submitCommandBuffers(graphicsQ);
	waitAndRecycleLastFence(resources.getLastSubmittedFence(), graphicsQ, device);
}

void Environment::dispatchHDRToCubemap(VkCommandBuffer cmd, ImageLUTEntry entry, PipelineObj& pipeline, PipelineLayoutConst layout) {

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline);

	struct alignas(16) PC {
		uint32_t equirectViewIdx;
		uint32_t skyboxStorageIdx;
		uint32_t pad0[2];
	} pc{};

	pc.equirectViewIdx = entry.combinedImageIndex;
	pc.skyboxStorageIdx = entry.storageImageIndex;

	vkCmdPushConstants(cmd, layout.layout, layout.pcRange.stageFlags, layout.pcRange.offset, layout.pcRange.size, &pc);

	const auto xDispatch = static_cast<uint32_t>(std::ceil(Environment::CUBEMAP_EXTENTS.width / 16.0f));
	const auto yDispatch = static_cast<uint32_t>(std::ceil(Environment::CUBEMAP_EXTENTS.height / 16.0f));

	vkCmdDispatch(cmd, xDispatch, yDispatch, 6);
}

void Environment::dispatchDiffuseIrradiance(VkCommandBuffer cmd, ImageLUTEntry entry, PipelineObj& pipeline, PipelineLayoutConst layout) {

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline);

	struct alignas(16) PC {
		float sampleCount;
		uint32_t skyboxViewIdx;
		uint32_t diffuseStorageIdx;
		uint32_t pad0;
	} pc{};

	pc.sampleCount = DIFFUSE_SAMPLE_DELTA;
	pc.skyboxViewIdx = entry.samplerCubeIndex;
	pc.diffuseStorageIdx = entry.storageImageIndex;

	vkCmdPushConstants(cmd, layout.layout, layout.pcRange.stageFlags, layout.pcRange.offset, layout.pcRange.size, &pc);

	const auto xDispatch = static_cast<uint32_t>(std::ceil(Environment::DIFFUSE_IRRADIANCE_BASE_EXTENTS.width / 8.0f));
	const auto yDispatch = static_cast<uint32_t>(std::ceil(Environment::DIFFUSE_IRRADIANCE_BASE_EXTENTS.height / 8.0f));

	vkCmdDispatch(cmd, xDispatch, yDispatch, 6);
}

void Environment::dispatchPrefilterEnvmap(VkCommandBuffer cmd, std::vector<SpecularPC> pushConstants, PipelineObj& pipeline,
	PipelineLayoutConst layout) {

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline);

	for (auto& pc : pushConstants) {
		vkCmdPushConstants(cmd, layout.layout, layout.pcRange.stageFlags, layout.pcRange.offset, layout.pcRange.size, &pc);

		const auto xDispatch = (pc.width + 7) / 8;
		const auto yDispatch = (pc.height + 7) / 8;

		vkCmdDispatch(cmd, xDispatch, yDispatch, 6);
	}
}

void Environment::dispatchBRDFLUT(VkCommandBuffer cmd, ImageLUTEntry entry, PipelineObj& pipeline, PipelineLayoutConst layout) {

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline);

	struct alignas(16) PC {
		uint32_t sampleCount;
		uint32_t brdfViewIdx;
		uint32_t pad0[2];
	} pc{};

	pc.sampleCount = PREFILTER_SAMPLE_COUNT;
	pc.brdfViewIdx = entry.storageImageIndex;

	vkCmdPushConstants(cmd, layout.layout, layout.pcRange.stageFlags, layout.pcRange.offset, layout.pcRange.size, &pc);

	const auto xDispatch = static_cast<uint32_t>(std::ceil(Environment::LUT_IMAGE_EXTENT.width / 8.0f));
	const auto yDispatch = static_cast<uint32_t>(std::ceil(Environment::LUT_IMAGE_EXTENT.height / 8.0f));

	vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
}
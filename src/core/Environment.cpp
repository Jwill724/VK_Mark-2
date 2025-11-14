#include "pch.h"

#include "Environment.h"
#include "engine/Engine.h"
#include "AssetManager.h"
#include "renderer/Renderer.h"
#include "renderer/Passes/RenderPasses.h"

struct alignas(16) EnvData {
	float sampleCountF = 0.0f;
	uint32_t sampleCountU = 0u;
	uint32_t pad0[2];
};

// Practically all environment shaders are based off this
// https://www.williscool.com/technical/environmentMapping.md.html

namespace Environment {
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
	GPUResources& resources)
{
	std::vector<const char*> hdrPaths = {
		"res/assets/envhdr/san_giuseppe_bridge_4k.hdr",
		"res/assets/envhdr/rogland_clear_night_4k.hdr",
		"res/assets/envhdr/meadow_4k.hdr",
		"res/assets/envhdr/belfast_sunset_puresky_4k.hdr",
		"res/assets/envhdr/kloppenheim_06_puresky_4k.hdr",
		"res/assets/envhdr/wasteland_clouds_4k.hdr"
	};

	auto skyboxSmpl = ResourceManager::getSkyBoxSampler();
	auto irradianceSmpl = ResourceManager::getIrradianceSampler();
	auto specSmpl = ResourceManager::getSpecularPrefilterSampler();
	auto& brdfImg = ResourceManager::getBRDFImage();

	auto& mainDQueue = resources.getMainDQueue();
	const auto alloc = resources.getAllocator();

	auto& graphicsPool = resources.getGraphicsPool();
	auto& graphicsQ = Backend::getGraphicsQueue();

	uint32_t setCount = 0u;
	for (const char* path : hdrPaths) {
		EnvironmentSet env = ResourceManager::initEnvironmentSetImages(
			device,
			mainDQueue,
			alloc);
		env.setIndex = setCount;

		env.equirect = loadHDR(path,
			graphicsPool,
			resources.getTempDQueue(),
			resources.getTempDQueue(),
			alloc,
			device);

		// Storage view defined per mip level
		const uint32_t specMipLevels = env.specular.mipLevelCount;
		env.specularPCs.resize(specMipLevels);
		for (uint32_t mip = 0; mip < specMipLevels; ++mip) {
			float roughness = static_cast<float>(mip) / static_cast<float>(specMipLevels - 1);

			env.specularPCs[mip].sampleCount = PREFILTER_SAMPLE_COUNT;
			env.specularPCs[mip].roughness = roughness;
			env.specularPCs[mip].width = std::max(1u, env.specular.imageExtent.width >> mip);
			env.specularPCs[mip].height = std::max(1u, env.specular.imageExtent.height >> mip);
		}

		ResourceManager::_environmentSets[setCount++] = env;
	}

	DescriptorWriter writer;

	CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {
		RenderPasses::ComputeDispatchScope envScope;
		EnvData envData;

		for (auto& env : ResourceManager::_environmentSets) {
			if (env.setIndex == UINT32_MAX) break;

			auto& equirect = env.equirect;
			auto& skyboxImg = env.skybox;
			auto& specularImg = env.specular;
			auto& irradianceImg = env.irradiance;

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
				specularImg.image,
				specularImg.imageFormat,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_GENERAL);
			ImageUtils::transitionImage(cmd,
				irradianceImg.image,
				irradianceImg.imageFormat,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_GENERAL);

			// EQUIRECT TO CUBEMAP
			writer.writePushImage(
				PUSH_BINDING_INPUT_1_TEX,
				equirect.imageView,
				skyboxSmpl);
			writer.writePushImage(
				PUSH_BINDING_OUTPUT_TEX,
				skyboxImg.storageView);

			envScope.setPush(&envData); // Attach pointer once
			envScope.extent = { skyboxImg.imageExtent.width, skyboxImg.imageExtent.height };
			envScope.workgroupSize = { 16u, 16u, 6u }; // Only pass that needs 16x16

			RenderPasses::dispatchComputePass(
				cmd,
				Pipelines::getHandle(PipelineID::HDRToCubemap),
				envScope,
				writer);
			ImageUtils::transitionImage(cmd,
				skyboxImg.image,
				skyboxImg.imageFormat,
				VK_IMAGE_LAYOUT_GENERAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			ImageUtils::generateCubemapMiplevels(cmd, skyboxImg);

			// DIFFUSE IRRADIANCE
			writer.writePushImage(
				PUSH_BINDING_INPUT_1_TEX,
				skyboxImg.imageView,
				irradianceSmpl);

			writer.writePushImage(
				PUSH_BINDING_OUTPUT_TEX,
				irradianceImg.storageView);

			envData.sampleCountF = DIFFUSE_SAMPLE_DELTA;
			envScope.extent = { irradianceImg.imageExtent.width, irradianceImg.imageExtent.height };
			envScope.workgroupSize = { 8u, 8u, 6u }; // Size for irradiance and specular

			RenderPasses::dispatchComputePass(
				cmd,
				Pipelines::getHandle(PipelineID::DiffuseIrradiance),
				envScope,
				writer);
			ImageUtils::transitionImage(cmd,
				irradianceImg.image,
				irradianceImg.imageFormat,
				VK_IMAGE_LAYOUT_GENERAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			// SPECULAR PREFILTER
			for (uint32_t i = 0; i < specularImg.mipLevelCount; ++i) {
				writer.writePushImage(
					PUSH_BINDING_INPUT_1_TEX,
					skyboxImg.imageView,
					specSmpl);

				writer.writePushImage(
					PUSH_BINDING_OUTPUT_TEX,
					specularImg.storageViews[i]);

				envScope.extent = { env.specularPCs[i].width, env.specularPCs[i].height };
				envScope.setPush(&env.specularPCs[i]);

				RenderPasses::dispatchComputePass(
					cmd,
					Pipelines::getHandle(PipelineID::SpecularPrefilter),
					envScope,
					writer);
			}

			ImageUtils::transitionImage(cmd,
				specularImg.image,
				specularImg.imageFormat,
				VK_IMAGE_LAYOUT_GENERAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}

		// BRDF
		writer.writePushImage(
			PUSH_BINDING_OUTPUT_TEX,
			brdfImg.storageView);

		envData.sampleCountU = PREFILTER_SAMPLE_COUNT;
		envScope.setPush(&envData);
		envScope.workgroupSize = { 8u, 8u, 1u };
		envScope.extent = { brdfImg.imageExtent.width, brdfImg.imageExtent.height };

		ImageUtils::transitionImage(cmd,
			brdfImg.image,
			brdfImg.imageFormat,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL);
		RenderPasses::dispatchComputePass(
			cmd,
			Pipelines::getHandle(PipelineID::BRDFLUT),
			envScope,
			writer
		);
		ImageUtils::transitionImage(cmd,
			brdfImg.image,
			brdfImg.imageFormat,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	}, graphicsPool, QueueType::Graphics, device);

	resources.getLastSubmittedFence() = Engine::getState().submitCommandBuffers(graphicsQ);
	waitAndRecycleLastFence(resources.getLastSubmittedFence(), graphicsQ, device);
}
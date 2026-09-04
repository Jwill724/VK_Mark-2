#include "pch.h"

#include "NRDContext.h"
#include "../Device.h"
#include "../ImageUtils.h"
#include "../descriptors/DescriptorWriter.h"
#include "../memory/ResourceAllocator.h"
#include "../memory/BindlessImageTable.h"
#include "../../rendergraph/scopes/ComputeScope.h"
#include "../../../common/EngineTypes.h"

#include <glm/gtc/type_ptr.hpp>

#include "NRD.h"
#include "NRDDescs.h"
#include "NRDSettings.h"

namespace
{
	inline const nrd::InstanceDesc& InstDesc(nrd::Instance& inst) { return *nrd::GetInstanceDesc(inst); }
	inline const nrd::LibraryDesc& LibDesc() { return *nrd::GetLibraryDesc(); }

	Vulkan_Format ToEngine(nrd::Format f)
	{
		using F = nrd::Format;
		switch (f)
		{
		case F::R8_UNORM:             return Vulkan_Format::R8unorm;
		case F::R8_UINT:              return Vulkan_Format::R8U;
		case F::RG8_UNORM:            return Vulkan_Format::RG8unorm;
		case F::RGBA8_UNORM:          return Vulkan_Format::RGBA8unorm;
		case F::RGBA8_SRGB:           return Vulkan_Format::RGBA8srgb;
		case F::R16_UNORM:            return Vulkan_Format::R16unorm;
		case F::R16_UINT:             return Vulkan_Format::R16U;
		case F::R16_SFLOAT:           return Vulkan_Format::R16F;
		case F::RG16_UNORM:           return Vulkan_Format::RG16unorm;
		case F::RG16_SNORM:           return Vulkan_Format::RG16snorm;
		case F::RG16_SFLOAT:          return Vulkan_Format::RG16F;
		case F::RGBA16_SNORM:         return Vulkan_Format::RGBA16snorm;
		case F::RGBA16_SFLOAT:        return Vulkan_Format::RGBA16F;
		case F::R32_UINT:             return Vulkan_Format::R32U;
		case F::R32_SFLOAT:           return Vulkan_Format::R32F;
		case F::RG32_UINT:            return Vulkan_Format::RG32U;
		case F::RG32_SFLOAT:          return Vulkan_Format::RG32F;
		case F::RGBA32_SFLOAT:        return Vulkan_Format::RGBA32F;
		case F::R10_G10_B10_A2_UNORM: return Vulkan_Format::ABGRpacked;
		case F::R11_G11_B10_UFLOAT:   return Vulkan_Format::BGRpacked;
		default: break;
		}

		ASSERT(false && "nrd::Format has no Vulkan_Format mapping");
		return Vulkan_Format::Undefined;
	}

	VkSampler MakeNRDSampler(VkDevice device, nrd::Sampler s)
	{
		const bool linear = (s == nrd::Sampler::LINEAR_CLAMP);
		return ImageUtils::CreateSampler(
			device,
			linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			VK_LOD_CLAMP_NONE,
			1.0f,
			linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST);
	}

	void NRDDispatchBarrier(VkCommandBuffer cmd)
	{
		VkMemoryBarrier2 mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
		mb.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		mb.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
		mb.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		mb.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
			VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
			VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

		VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		di.memoryBarrierCount = 1u;
		di.pMemoryBarriers = &mb;
		vkCmdPipelineBarrier2(cmd, &di);
	}
}

// ==============
// === Setup ====
// ==============

void NRDContext::Init(const Device& device, Allocator& allocator, Extents2D extent, DenoiserMode mode)
{
	m_device = device.GetContext().device;
	m_vma = allocator.GetVma();
	m_extent = extent;
	m_mode = mode;
	m_maxPushDescriptors = device.GetProperties().pushDescriptor.maxPushDescriptors;
	m_bAccumulationReset = true;

	const nrd::DenoiserDesc reflectDenoisers[] = { { SPEC_ID,  nrd::Denoiser::REBLUR_SPECULAR } };
	const nrd::DenoiserDesc shadowDenoisers[] = { { SHADOW_ID, nrd::Denoiser::SIGMA_SHADOW } };

	nrd::InstanceCreationDesc creation{};
	creation.denoisers = (mode == DenoiserMode::Shadows) ? shadowDenoisers : reflectDenoisers;
	creation.denoisersNum = 1u;

	INVARIANT(nrd::CreateInstance(creation, m_instance) == nrd::Result::SUCCESS);

	INVARIANT(LibDesc().normalEncoding == nrd::NormalEncoding::R10_G10_B10_A2_UNORM);
	INVARIANT(LibDesc().roughnessEncoding == nrd::RoughnessEncoding::LINEAR);

	CreateSamplers(m_device);
	CreateLayoutsAndPipelines(m_device);
	CreateConstantRing(device, allocator);
	CreatePools(allocator, m_extent);
}

void NRDContext::Resize(Allocator& allocator, Extents2D extent)
{
	INVARIANT(extent.Width() > 0u && extent.Height() > 0u);

	DestroyPools(allocator);

	if (m_instance)
	{
		nrd::DestroyInstance(*m_instance);
		m_instance = nullptr;
	}

	const nrd::DenoiserDesc reflectDenoisers[] = { { SPEC_ID,  nrd::Denoiser::REBLUR_SPECULAR } };
	const nrd::DenoiserDesc shadowDenoisers[] = { { SHADOW_ID, nrd::Denoiser::SIGMA_SHADOW } };

	nrd::InstanceCreationDesc creation{};
	creation.denoisers = (m_mode == DenoiserMode::Shadows) ? shadowDenoisers : reflectDenoisers;
	creation.denoisersNum = 1u;

	INVARIANT(nrd::CreateInstance(creation, m_instance) == nrd::Result::SUCCESS);

	m_extent = extent;
	CreatePools(allocator, m_extent);

	m_io = {};
	m_ringCursor = 0u;
	m_lastCBOffset = 0u;
	m_frameSlot = 0u;
	m_nrdFrameIndex = 0;
	m_prevProjUnjittered = glm::mat4(1.0f);
	m_smoothedDeltaSeconds = 0.0f;
	m_bAccumulationReset = true;
}

void NRDContext::Shutdown(VkDevice device, Allocator& allocator)
{
	DestroyPools(allocator);

	if (m_constantRing.IsValid())
	{
		allocator.FreeBuffer(m_constantRing);
		m_constantRing.Reset();
	}

	for (const PipelineHandle& h : m_pipelines)
	{
		if (h.pipeline != VK_NULL_HANDLE)
			vkDestroyPipeline(device, h.pipeline, nullptr);
		if (h.layout.pipelineLayout != VK_NULL_HANDLE)
			vkDestroyPipelineLayout(device, h.layout.pipelineLayout, nullptr);
	}
	for (VkDescriptorSetLayout l : m_resourceSetLayouts)
		vkDestroyDescriptorSetLayout(device, l, nullptr);
	for (VkSampler s : m_samplers)
		vkDestroySampler(device, s, nullptr);

	if (m_cbSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, m_cbSetLayout, nullptr);
	if (m_cbPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, m_cbPool, nullptr);

	m_resourceSetLayouts.clear();
	m_pipelines.clear();
	m_samplers.clear();
	m_nrdFrameIndex = 0;

	if (m_instance)
	{
		nrd::DestroyInstance(*m_instance);
		m_instance = nullptr;
	}
}

void NRDContext::CreateSamplers(VkDevice device)
{
	const nrd::InstanceDesc& desc = InstDesc(*m_instance);

	m_samplers.reserve(desc.samplersNum);
	for (uint32_t i = 0; i < desc.samplersNum; ++i)
		m_samplers.push_back(MakeNRDSampler(device, desc.samplers[i]));
}

void NRDContext::CreateLayoutsAndPipelines(VkDevice device)
{
	const nrd::InstanceDesc& desc = InstDesc(*m_instance);
	const auto& shifts = LibDesc().spirvBindingOffsets;

	m_cbSetIndex = desc.constantBufferAndSamplersSpaceIndex;
	m_resourceSetIndex = desc.resourcesSpaceIndex;
	m_setCount = std::max(m_cbSetIndex, m_resourceSetIndex) + 1u;

	INVARIANT(m_cbSetIndex != m_resourceSetIndex || m_setCount == 1u);

	const nrd::DescriptorPoolDesc& pool = desc.descriptorPoolDesc;
	INVARIANT(pool.perSetTexturesMaxNum + pool.perSetStorageTexturesMaxNum <= m_maxPushDescriptors);

	// --- shared CB + immutable sampler set ---
	{
		std::vector<VkDescriptorSetLayoutBinding> bindings;

		bindings.emplace_back(VkDescriptorSetLayoutBinding{
			.binding = shifts.constantBufferOffset + desc.constantBufferRegisterIndex,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.descriptorCount = 1u,
			.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT });

		for (uint32_t s = 0; s < desc.samplersNum; ++s)
		{
			bindings.emplace_back(VkDescriptorSetLayoutBinding{
				.binding = shifts.samplerOffset + desc.samplersBaseRegisterIndex + s,
				.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
				.descriptorCount = 1u,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = &m_samplers[s] });
		}

		VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		info.bindingCount = static_cast<uint32_t>(bindings.size());
		info.pBindings = bindings.data();
		VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &m_cbSetLayout));

		const VkDescriptorPoolSize sizes[] = {
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1u },
			{ VK_DESCRIPTOR_TYPE_SAMPLER, std::max(desc.samplersNum, 1u) }
		};

		VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		poolInfo.maxSets = 1u;
		poolInfo.poolSizeCount = 2u;
		poolInfo.pPoolSizes = sizes;
		VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_cbPool));

		VkDescriptorSetAllocateInfo alloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		alloc.descriptorPool = m_cbPool;
		alloc.descriptorSetCount = 1u;
		alloc.pSetLayouts = &m_cbSetLayout;
		VK_CHECK(vkAllocateDescriptorSets(device, &alloc, &m_cbSet));
	}

	m_resourceSetLayouts.resize(desc.pipelinesNum, VK_NULL_HANDLE);
	m_pipelines.resize(desc.pipelinesNum, PipelineHandle{});

	for (uint32_t p = 0; p < desc.pipelinesNum; ++p)
	{
		const nrd::PipelineDesc& pd = desc.pipelines[p];

		std::vector<VkDescriptorSetLayoutBinding> bindings;
		uint32_t texReg = 0u;
		uint32_t uavReg = 0u;

		for (uint32_t r = 0; r < pd.resourceRangesNum; ++r)
		{
			const nrd::ResourceRangeDesc& range = pd.resourceRanges[r];
			const bool storage = (range.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE);

			for (uint32_t n = 0; n < range.descriptorsNum; ++n)
			{
				const uint32_t reg = storage ? uavReg++ : texReg++;

				bindings.emplace_back(VkDescriptorSetLayoutBinding{
					.binding = (storage ? shifts.storageTextureAndBufferOffset : shifts.textureOffset)
							 + desc.resourcesBaseRegisterIndex + reg,
					.descriptorType = storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
											   : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
					.descriptorCount = 1u,
					.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT });
			}
		}

		VkDescriptorSetLayoutCreateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		setInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT;
		setInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		setInfo.pBindings = bindings.data();
		VK_CHECK(vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &m_resourceSetLayouts[p]));

		INVARIANT(m_cbSetIndex != m_resourceSetIndex &&
			"NRD built with a single register space; this integration assumes two sets.");

		std::vector<VkDescriptorSetLayout> layouts(m_setCount, VK_NULL_HANDLE);
		layouts[m_cbSetIndex] = m_cbSetLayout;
		layouts[m_resourceSetIndex] = m_resourceSetLayouts[p];

		VkPipelineLayout pipeLayout = VK_NULL_HANDLE;
		VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		layoutInfo.setLayoutCount = m_setCount;
		layoutInfo.pSetLayouts = layouts.data();
		VK_CHECK(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipeLayout));

		INVARIANT(pd.computeShaderSPIRV.bytecode != nullptr);

		VkShaderModuleCreateInfo moduleInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		moduleInfo.codeSize = static_cast<size_t>(pd.computeShaderSPIRV.size);
		moduleInfo.pCode = static_cast<const uint32_t*>(pd.computeShaderSPIRV.bytecode);

		VkShaderModule module = VK_NULL_HANDLE;
		VK_CHECK(vkCreateShaderModule(device, &moduleInfo, nullptr, &module));

		VkComputePipelineCreateInfo pipeInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
		pipeInfo.layout = pipeLayout;
		pipeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		pipeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		pipeInfo.stage.module = module;
		pipeInfo.stage.pName = desc.shaderEntryPoint;

		VkPipeline pipe = VK_NULL_HANDLE;
		VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1u, &pipeInfo, nullptr, &pipe));
		vkDestroyShaderModule(device, module, nullptr);

		PipelineHandle& handle = m_pipelines[p];
		handle.pipeline = pipe;
		handle.bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
		handle.layout.pipelineLayout = pipeLayout;
		handle.layout.pushConstantDef = { 0u, 0u, 0u };
	}
}

void NRDContext::CreateConstantRing(const Device& device, Allocator& allocator)
{
	const nrd::InstanceDesc& desc = InstDesc(*m_instance);
	const auto& shifts = LibDesc().spirvBindingOffsets;

	const size_t align = device.GetProperties().limits.minUniformBufferOffsetAlignment;
	m_constantStride = static_cast<uint32_t>(
		AllocatedBuffer::AlignUp(desc.constantBufferMaxDataSize, align));

	m_constantSliceBytes = m_constantStride * MAX_CB_DISPATCHES;

	BufferDesc bufDesc{};
	bufDesc.size = static_cast<size_t>(m_constantSliceBytes) * RING_FRAMES;
	bufDesc.usage = Vulkan_BufferUsage::UNIFORM;
	bufDesc.heap = HeapType::Upload;
	bufDesc.debugName = "NRDConstantRing";

	m_constantRing = allocator.AllocateBuffer(bufDesc);
	INVARIANT(m_constantRing.m_mappedPtr != nullptr);

	INVARIANT(m_cbSet != VK_NULL_HANDLE);

	VkDescriptorBufferInfo cbInfo{ m_constantRing.m_buffer, 0u, desc.constantBufferMaxDataSize };

	VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	write.dstSet = m_cbSet;
	write.dstBinding = shifts.constantBufferOffset + desc.constantBufferRegisterIndex;
	write.descriptorCount = 1u;
	write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	write.pBufferInfo = &cbInfo;

	vkUpdateDescriptorSets(m_device, 1u, &write, 0u, nullptr);
}

void NRDContext::CreatePools(Allocator& allocator, Extents2D extent)
{
	const nrd::InstanceDesc& desc = InstDesc(*m_instance);

	auto build = [&](const nrd::TextureDesc* descs, uint32_t count,
		std::vector<AllocatedImage>& out, const char* tag)
		{
			out.resize(count);
			for (uint32_t i = 0; i < count; ++i)
			{
				const nrd::TextureDesc& td = descs[i];
				const uint32_t f = std::max<uint32_t>(td.downsampleFactor, 1u);

				ImageDesc imgDesc{};
				imgDesc.format = ToEngine(td.format);
				imgDesc.extent = { std::max(1u, (extent.Width() + f - 1u) / f),
										   std::max(1u, (extent.Height() + f - 1u) / f), 1u };
				imgDesc.usage = Vulkan_ImageUsage::ComputeOnly;
				imgDesc.mipLevels = 1u;
				imgDesc.bPerMipStorage = false;
				imgDesc.debugName = fmt::format("{}_{}", tag, i);
				imgDesc.bIsConcurrent = true;

				out[i] = allocator.AllocateImage(imgDesc);
			}
		};

	build(desc.permanentPool, desc.permanentPoolSize, m_permanentPool, "NRDPermanent");
	build(desc.transientPool, desc.transientPoolSize, m_transientPool, "NRDTransient");
}

void NRDContext::DestroyPools(Allocator& allocator)
{
	auto kill = [&](std::vector<AllocatedImage>& pool)
		{
			for (auto& img : pool)
				if (img.IsValid()) { allocator.FreeImage(img); img.Reset(); }
			pool.clear();
		};

	kill(m_permanentPool);
	kill(m_transientPool);
}

void NRDContext::RecordPoolInit(VkCommandBuffer cmd)
{
	std::vector<VkImageMemoryBarrier2> barriers;
	barriers.reserve(m_permanentPool.size() + m_transientPool.size() + 1u);

	auto addOne = [&](const AllocatedImage& img)
		{
			if (!img.IsValid()) return;

			VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
			b.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
			b.srcAccessMask = VK_ACCESS_2_NONE;
			b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			b.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
				VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
				VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
			b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
			b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.image = img.m_image;
			b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };

			barriers.push_back(b);
		};

	for (const AllocatedImage& img : m_permanentPool) addOne(img);
	for (const AllocatedImage& img : m_transientPool) addOne(img);

	VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	dep.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
	dep.pImageMemoryBarriers = barriers.data();
	vkCmdPipelineBarrier2(cmd, &dep);
}

// =================
// === Per frame ===
// =================

VkImageView NRDContext::ResolveView(const nrd::ResourceDesc& res) const
{
	switch (res.type)
	{
	case nrd::ResourceType::PERMANENT_POOL:            return m_permanentPool[res.indexInPool].m_imageView;
	case nrd::ResourceType::TRANSIENT_POOL:            return m_transientPool[res.indexInPool].m_imageView;
	case nrd::ResourceType::IN_MV:                     return m_io.motion;
	case nrd::ResourceType::IN_NORMAL_ROUGHNESS:       return m_io.normalRoughness;
	case nrd::ResourceType::IN_VIEWZ:                  return m_io.viewZ;
	case nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST:  return m_io.specRadianceIn;
	case nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST: return m_io.specRadianceOut;
	case nrd::ResourceType::IN_PENUMBRA:               return m_io.penumbra;
	case nrd::ResourceType::OUT_SHADOW_TRANSLUCENCY:   return m_io.shadowOut;
	default: break;
	}

	ASSERT(false && "NRD requested an unbound resource type");
	return VK_NULL_HANDLE;
}

void NRDContext::SetFrameSettings(
	const SceneInfo& scene,
	const BindlessImageTable& imageTable,
	float deltaSeconds,
	bool historyValid)
{
	//m_frameSlot = m_nrdFrameIndex % RING_FRAMES;
	m_frameSlot = scene.temporal.x % RING_FRAMES;
	m_ringCursor = 0u;
	m_lastCBOffset = 0u;

	if (m_mode == DenoiserMode::Shadows)
	{
		m_io.motion = imageTable.GetRenderTarget(RD::Renderer_RenderTarget::Velocity).m_imageView;

		m_io.normalRoughness = imageTable.GetRenderTarget(RD::Renderer_RenderTarget::NRDShadowNormalRoughness).m_imageView;
		m_io.viewZ = imageTable.GetRenderTarget(RD::Renderer_RenderTarget::NRDShadowViewZ).m_imageView;
		m_io.penumbra = imageTable.GetRenderTarget(RD::Renderer_RenderTarget::RTShadowPenumbra).m_imageView;
		m_io.shadowOut = imageTable.GetRenderTarget(RD::Renderer_RenderTarget::RTShadowDenoised).m_imageView;

		INVARIANT(m_io.penumbra != m_io.shadowOut);
	}
	else
	{
		m_io.motion = imageTable.GetRenderTarget(RD::Renderer_RenderTarget::NRDMotion).m_imageView;
		m_io.normalRoughness = imageTable.GetRenderTarget(RD::Renderer_RenderTarget::NRDNormalRoughness).m_imageView;
		m_io.viewZ = imageTable.GetRenderTarget(RD::Renderer_RenderTarget::NRDViewZ).m_imageView;
		m_io.specRadianceIn = imageTable.GetRenderTarget(RD::Renderer_RenderTarget::ReflectRadiance).m_imageView;
		m_io.specRadianceOut = imageTable.GetRenderTarget(RD::Renderer_RenderTarget::RTReflectDenoised).m_imageView;

		INVARIANT(m_io.specRadianceIn != m_io.specRadianceOut);
	}

	const float w = static_cast<float>(m_extent.Width());
	const float h = static_cast<float>(m_extent.Height());

	nrd::CommonSettings common{};

	std::memcpy(common.viewToClipMatrix, glm::value_ptr(scene.projUnjittered), sizeof(float) * 16);
	std::memcpy(common.viewToClipMatrixPrev, glm::value_ptr(m_prevProjUnjittered), sizeof(float) * 16);
	std::memcpy(common.worldToViewMatrix, glm::value_ptr(scene.view), sizeof(float) * 16);
	std::memcpy(common.worldToViewMatrixPrev, glm::value_ptr(scene.prevView), sizeof(float) * 16);

	const float mvSign = (m_mode == DenoiserMode::Shadows) ? -1.0f : 1.0f;
	common.motionVectorScale[0] = mvSign;
	common.motionVectorScale[1] = mvSign;
	common.motionVectorScale[2] = 0.0f;

	if (m_bAccumulationReset)
	{
		common.cameraJitter[0] = 0.0f;
		common.cameraJitter[1] = 0.0f;
		common.cameraJitterPrev[0] = 0.0f;
		common.cameraJitterPrev[1] = 0.0f;
	}
	else
	{
		// temporalJitter is NDC, NRD wants pixels of rectSize; NDC +y is screen-up, UV +y is down
		common.cameraJitter[0] = std::clamp(
			scene.temporalJitter.x * 0.5f * w, -0.5f * w, 0.5f * w);
		common.cameraJitter[1] = std::clamp(
			-scene.temporalJitter.y * 0.5f * h, -0.5f * h, 0.5f * h);
		common.cameraJitterPrev[0] = std::clamp(
			scene.temporalJitter.z * 0.5f * w, -0.5f * w, 0.5f * w);
		common.cameraJitterPrev[1] = std::clamp(
			-scene.temporalJitter.w * 0.5f * h, -0.5f * h, 0.5f * h);
	}

	const uint16_t rw = static_cast<uint16_t>(m_extent.Width());
	const uint16_t rh = static_cast<uint16_t>(m_extent.Height());

	common.resourceSize[0] = common.resourceSizePrev[0] = common.rectSize[0] = common.rectSizePrev[0] = rw;
	common.resourceSize[1] = common.resourceSizePrev[1] = common.rectSize[1] = common.rectSizePrev[1] = rh;

	common.viewZScale = 1.0f;
	common.denoisingRange = scene.cameraClips.y;
	common.disocclusionThreshold = 0.01f;
	common.timeDeltaBetweenFrames = deltaSeconds * 1000.0f;
	//common.frameIndex = m_nrdFrameIndex++;
	common.frameIndex = scene.temporal.x;
	common.isMotionVectorInWorldSpace = false;
	common.accumulationMode = m_bAccumulationReset
		? nrd::AccumulationMode::CLEAR_AND_RESTART
		: (historyValid ? nrd::AccumulationMode::CONTINUE
			: nrd::AccumulationMode::RESTART);

	m_bAccumulationReset = false;

	INVARIANT(nrd::SetCommonSettings(*m_instance, common) == nrd::Result::SUCCESS);

	m_smoothedDeltaSeconds = glm::mix(m_smoothedDeltaSeconds, deltaSeconds, 0.05f);
	const float fps = 1.0f / std::max(m_smoothedDeltaSeconds, 1e-4f);

	if (m_mode == DenoiserMode::Shadows)
	{
		const glm::vec3 sunTravel = glm::normalize(glm::vec3(scene.sunlightDirection));

		nrd::SigmaSettings sigma{};
		std::memcpy(sigma.lightDirection, glm::value_ptr(sunTravel), sizeof(float) * 3);

		sigma.maxStabilizedFrameNum = std::min(
			nrd::GetMaxAccumulatedFrameNum(nrd::SIGMA_DEFAULT_ACCUMULATION_TIME, fps),
			nrd::SIGMA_MAX_HISTORY_FRAME_NUM);

		INVARIANT(nrd::SetDenoiserSettings(*m_instance, SHADOW_ID, &sigma) == nrd::Result::SUCCESS);
	}
	else
	{
		nrd::ReblurSettings reblur{};
		reblur.hitDistanceParameters = { 3.0f, 0.1f, 20.0f };
		reblur.maxAccumulatedFrameNum = std::min(
			nrd::GetMaxAccumulatedFrameNum(nrd::REBLUR_DEFAULT_ACCUMULATION_TIME, fps),
			nrd::REBLUR_MAX_HISTORY_FRAME_NUM);
		reblur.maxFastAccumulatedFrameNum = std::max(reblur.maxAccumulatedFrameNum / 6u, 1u);
		reblur.maxStabilizedFrameNum = reblur.maxAccumulatedFrameNum;
		reblur.fastHistoryClampingSigmaScale = 2.0f;

		INVARIANT(nrd::SetDenoiserSettings(*m_instance, SPEC_ID, &reblur) == nrd::Result::SUCCESS);
	}

	m_prevProjUnjittered = scene.projUnjittered;
}

void NRDContext::RecordDispatches(
	VkCommandBuffer cmd,
	ComputeScope& scope,
	PushDescriptorWriter& writer) const
{
	const nrd::DispatchDesc* dispatches = nullptr;
	uint32_t                 dispatchNum = 0u;
	const nrd::Identifier id = (m_mode == DenoiserMode::Shadows) ? SHADOW_ID : SPEC_ID;

	INVARIANT(nrd::GetComputeDispatches(*m_instance, &id, 1u, dispatches, dispatchNum) == nrd::Result::SUCCESS);

	const nrd::InstanceDesc& desc = InstDesc(*m_instance);
	ASSERT(desc.pipelinesNum == m_pipelines.size());
	ASSERT(desc.permanentPoolSize + desc.transientPoolSize
		== m_permanentPool.size() + m_transientPool.size());
	const auto& shifts = LibDesc().spirvBindingOffsets;
	const nrd::DescriptorPoolDesc& pool = desc.descriptorPoolDesc;

	scope.SkipPushConstant(true);
	writer.Reserve(pool.perSetTexturesMaxNum + pool.perSetStorageTexturesMaxNum + 1u);

	uint8_t* ringBase = static_cast<uint8_t*>(m_constantRing.m_mappedPtr);

	for (uint32_t i = 0; i < dispatchNum; ++i)
	{
		const nrd::DispatchDesc& d = dispatches[i];
		const nrd::PipelineDesc& pd = desc.pipelines[d.pipelineIndex];

		if (i > 0u) NRDDispatchBarrier(cmd);

		uint32_t texReg = 0u;
		uint32_t uavReg = 0u;

		for (uint32_t n = 0; n < d.resourcesNum; ++n)
		{
			const nrd::ResourceDesc& res = d.resources[n];
			VkImageView view = ResolveView(res);

			if (res.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE)
				writer.WritePushStorageImage(
					shifts.storageTextureAndBufferOffset + desc.resourcesBaseRegisterIndex + uavReg++,
					view);
			else
				writer.WritePushSampledImage(
					shifts.textureOffset + desc.resourcesBaseRegisterIndex + texReg++,
					view, NRD_LAYOUT);
		}

		if (pd.hasConstantData && !d.constantBufferDataMatchesPreviousDispatch)
		{
			ASSERT(m_ringCursor < MAX_CB_DISPATCHES);

			m_lastCBOffset = m_frameSlot * m_constantSliceBytes
				+ m_ringCursor * m_constantStride;
			++m_ringCursor;

			std::memcpy(ringBase + m_lastCBOffset,
				d.constantBufferData, d.constantBufferDataSize);
		}

		const PipelineHandle& handle = m_pipelines[d.pipelineIndex];

		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
			handle.layout.pipelineLayout, m_cbSetIndex, 1u, &m_cbSet,
			1u, &m_lastCBOffset);

		scope.UpdateWorkgroups({ d.gridWidth, d.gridHeight, 1u }, true);
		scope.DispatchComputePass(cmd, handle, writer, m_resourceSetIndex);
	}

	vmaFlushAllocation(
		m_vma,
		m_constantRing.m_allocation,
		m_frameSlot * m_constantSliceBytes,
		m_constantSliceBytes);
}

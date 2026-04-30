#pragma once

#include "profiler/Profiler.h"
#include "renderer/frame/FrameContext.h"
#include "renderer/backend/memory/Allocator.h"
#include "renderer/backend/memory/AllocatedImage.h"
#include "renderer/backend/memory/AllocatedBuffer.h"
#include "renderer/backend/Device.h"
#include "renderer/backend/PipelineManager.h"
#include "renderer/backend/Descriptor.h"
#include "Material.h"
#include "RendererDefinitions.h"
#include <array>

namespace RD = RendererDefinitions;

class FrameContext;
class Profiler;
struct ForwardPush;

class MeshRegistry;
class ImageLUTManager;
class BindlessBufferTable;
class BindlessImageTable;
class DeletionQueue;

class PipelineManager;
class DescriptorManager;

struct AllocatedBuffer;
struct AllocatedImage;

class Renderer
{
public:
	VkExtent3D GetDrawExtent() const { return m_drawExtent; }
	void SetDrawExtent(VkExtent3D extent) { m_drawExtent = extent; }

	uint32_t GetFrameNumber() const { return m_frameNumber; }

	bool IsFirstFrame() const noexcept { return m_frameNumber == 0; }

	FrameContext& GetCurrentFrame() const
	{
		return *m_frameContexts[m_frameNumber % m_framesInFlight];
	}

	FrameContext& GetLastFrame() const
	{
		uint32_t lastFrameNumber = m_frameNumber + m_framesInFlight - 1u;
		return *m_frameContexts[lastFrameNumber % m_framesInFlight];
	}

	//ForwardPush& GetForwardPush() { return m_forwardPush; }

	void Init();
	void Cleanup();

	void RecordRenderCommand();
	void PrepareFrame();
	void SubmitFrame();

private:
	uint32_t m_frameNumber = 0;
	uint32_t m_framesInFlight =  0;

	VkExtent3D m_drawExtent;

	void InitFrameResources();
	void CleanupFrameResources();

	std::vector<std::unique_ptr<FrameContext>> m_frameContexts;
	std::unique_ptr<BindlessBufferTable> m_globalAddressTable;

	//// Staging buffers
	//AllocatedBuffer m_addressTableStagingBuffer;
	//AllocatedBuffer m_lightListStagingBuffer;
	//AllocatedBuffer m_transformsStagingBuffer;

	// Runtime resource data
	std::vector<Material> m_materials;
	std::unique_ptr<MeshRegistry> m_registeredMeshes;
	std::unique_ptr<ImageLUTManager> m_lutManager;

	std::optional<BindlessBufferTable> m_globalImageManager;
	//EnvironmentSet m_environmentSets[MAX_ENV_SETS];
	EnvironmentIndexArray m_environmentMapIndices;

	std::vector<uint32_t> m_materialFlagsIDs;

	std::array<AllocatedImage, static_cast<size_t>(RD::Renderer_RenderTarget::Count)> m_renderTargets;
	std::array<AllocatedImage, static_cast<size_t>(RD::Renderer_Texture::Count)> m_textures;

	std::unique_ptr<DescriptorManager> m_descriptorManager;
	std::unique_ptr<PipelineManager> m_pipelineManager;
	std::unique_ptr<Allocator> m_allocator;
	std::unique_ptr<Device> m_device;

	DeletionQueue m_PersistentQueue;   // All static global vulkan state and resources for renderer lifetime
	DeletionQueue m_VolatileQueue;     // Will flush often
	DeletionQueue m_RenderTargetQueue; // Only flush on resized swapchain

	ForwardPush m_forwardPush{};

	Profiler m_profiler;
};

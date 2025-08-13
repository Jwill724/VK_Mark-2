#pragma once

#include "common/Vk_Types.h"
#include "common/ResourceTypes.h"
#include "common/EngineTypes.h"

namespace RendererUtils {
	VkSemaphore createSemaphore();
	void createTimelineSemaphore(TimelineSync& sync);
	VkFence createFence();

	// Within the context of my engine, theres only really types of images, render and textures.
	// Render being something I directly draw and textures going to the gpu, which will need a buffer to do gpu stuff.
	// Texture creation just defers the cmd work and buffer deletion
	// Create texture just holds createrenderimage inside it and skipping the use of a deletion queue is designed only for asset loading
	// since a ModelAsset type should own its resources
	void createTextureImage(
		VkCommandPool cmdPool,
		void* data,
		AllocatedImage& renderImage,
		VkImageUsageFlags usage,
		VkSampleCountFlagBits samples,
		DeletionQueue& imageQueue,
		DeletionQueue& bufferQueue,
		const VmaAllocator allocator,
		bool skipQueueUsage = false);
	void createRenderImage(AllocatedImage& renderImage, VkImageUsageFlags usage,
		VkSampleCountFlagBits samples, DeletionQueue& dq, const VmaAllocator alloc, bool skipDQ = false);

	void destroyImage(VkDevice device, AllocatedImage& img, const VmaAllocator allocator);

	void transitionImage(
		VkCommandBuffer cmd, VkImage image, VkFormat format,
		VkImageLayout currentLayout, VkImageLayout newLayout,
		VkPipelineStageFlags2 dstStageOverride  = 0,
		VkAccessFlags2        dstAccessOverride = 0);
	void copyImageToImage(VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent2D srcSize, VkExtent2D dstSize);

	uint32_t calculateMipLevels(AllocatedImage& img, uint32_t maxMipCap = UINT32_MAX);

	size_t getPixelSize(VkFormat format);

	VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);

	void generateCubemapMiplevels(VkCommandBuffer cmd, AllocatedImage& image);


	// === BUFFER BARRIERS ===

	// Map a QueueType to its family index (from Backend)
	uint32_t queueFamilyIndex(QueueType q);

	void releaseBuffer(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		VkPipelineStageFlags2 srcStage,
		VkAccessFlags2        srcAccess,
		uint32_t              srcFamily,
		uint32_t              dstFamily);

	void acquireBuffer(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		VkPipelineStageFlags2  dstStage,
		VkAccessFlags2         dstAccess,
		uint32_t               srcFamily,
		uint32_t               dstFamily);

	void releaseBufferQ(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		VkPipelineStageFlags2  srcStage,
		VkAccessFlags2         srcAccess,
		QueueType              srcQ,
		QueueType              dstQ);

	void acquireBufferQ(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		VkPipelineStageFlags2  dstStage,
		VkAccessFlags2         dstAccess,
		QueueType              srcQ,
		QueueType              dstQ);

	// convenience: transfer writes -> shader reads (uniform/storage)
	void releaseTransferToShaderReadQ(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		QueueType srcQ = QueueType::Transfer,
		QueueType dstQ = QueueType::Graphics);

	void acquireShaderReadQ(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		QueueType srcQ = QueueType::Transfer,
		QueueType dstQ = QueueType::Graphics);

	// convenience: transfer writes -> indirect draw reads
	void releaseTransferToIndirectQ(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		QueueType srcQ = QueueType::Transfer,
		QueueType dstQ = QueueType::Graphics);

	void acquireIndirectQ(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		QueueType srcQ = QueueType::Transfer,
		QueueType dstQ = QueueType::Graphics);

	// convenience: transfer writes -> vertex/index reads
	void releaseTransferToVertexIndexQ(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		QueueType srcQ = QueueType::Transfer,
		QueueType dstQ = QueueType::Graphics);

	void acquireVertexIndexQ(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		QueueType srcQ = QueueType::Transfer,
		QueueType dstQ = QueueType::Graphics);

	// compute producers
	void releaseComputeWriteQ(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		QueueType srcQ = QueueType::Compute,
		QueueType dstQ = QueueType::Graphics);

	void releaseComputeToIndirectQ(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		QueueType srcQ = QueueType::Compute,
		QueueType dstQ = QueueType::Graphics);
}
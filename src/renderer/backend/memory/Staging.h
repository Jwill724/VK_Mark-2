#pragma once

#include <renderer/backend/memory/VmaForward.h>
#include <renderer/backend/memory/AllocatedBuffer.h>

struct StagedWrite;
struct StagedTextureWrite;

struct AllocatedImage;
struct PendingTextureUpload;

class Allocator;

class StagingBuffer final
{
	friend class Allocator;
protected:
	StagingBuffer()  = default;
	~StagingBuffer() = default;
	StagingBuffer(const StagingBuffer&)            = delete;
	StagingBuffer& operator=(const StagingBuffer&) = delete;

	void Init(
		size_t size,
		VmaAllocator vma,
		VkDevice device,
		size_t nonCoherentAtomSize);
	void Shutdown();
public:
	// ---------------
	// Buffer staging
	// ---------------

	// Writes data into the staging buffer and returns a StagedWrite
	// ready for vkCmdCopyBuffer. Advances the internal head.
	[[nodiscard]] StagedWrite Stage(const void* data, size_t bytes, VkBuffer dst, VkDeviceSize dstOffset = 0);

	void CopyCommand(VkCommandBuffer cmd, StagedWrite write) noexcept;


	// ----------------
	// Texture staging
	// ----------------

	// Stages mip 0 pixel data for a single texture.
	// Returns a PendingTextureUpload the caller adds to their batch vector.
	// image must already be created as a GPU image (VK_IMAGE_LAYOUT_UNDEFINED).
	[[nodiscard]] PendingTextureUpload StageTexture(const void* data, size_t pixelBytes, AllocatedImage& image);

	// Stages all mip levels for a texture — use when you have pre-built mip data.
	// mipDatas[i] points to pixel data for mip level i.
	// mipExtents[i] is the extent of mip level i.
	[[nodiscard]] PendingTextureUpload StageTextureMips(
		const std::vector<const void*>& mipDatas,
		const std::vector<VkExtent3D>&  mipExtents,
		size_t                          pixelBytes,
		AllocatedImage&                 image);

	// Records all buffer-to-image copies for a single PendingTextureUpload.
	// Caller is responsible for image layout transitions before and after.
	void TextureCopyCommand(VkCommandBuffer cmd, const PendingTextureUpload& upload) const noexcept;

	// Records copies for an entire batch of pending texture uploads.
	void TextureCopyBatch(VkCommandBuffer cmd, const std::vector<PendingTextureUpload>& batch) const noexcept;


	// ------
	// Flush
	// ------

	// Flushes the entire written range since last Reset()
	// Call after all Stage() calls for the frame, before submitting
	void Flush() const;

	// Flushes a specific sub-range explicitly
	void FlushRange(size_t offset, size_t bytes) const;

	// Resets the head back to zero
	// Call at frame begin after the fence for this frame has been waited on
	void Reset() { m_head = 0; }

	// --------
	// Queries
	// --------
	size_t GetCapacity()   const noexcept { return m_capacity; }
	size_t GetUsed()       const noexcept { return m_head; }
	float  GetUsageRatio() const noexcept { return float(m_head) / float(m_capacity); }
	bool   IsValid()       const noexcept { return m_staging.IsValid(); }
private:
	size_t Suballocate(size_t bytes, size_t alignment);

	AllocatedBuffer m_staging{};
	size_t          m_capacity  = 0;
	size_t          m_head      = 0;
	size_t          m_atomSize  = 1;

	VmaAllocator    m_vma       = VK_NULL_HANDLE;
	VkDevice        m_device    = VK_NULL_HANDLE;
};

struct StagedWrite
{
	VkBuffer     srcBuffer = VK_NULL_HANDLE;
	VkBuffer     dstBuffer = VK_NULL_HANDLE;
	VkDeviceSize srcOffset = 0;
	VkDeviceSize dstOffset = 0;
	VkDeviceSize size      = 0;

	VkBufferCopy ToBufferCopy() const noexcept
	{
		return { srcOffset, dstOffset, size };
	}
};

struct StagedTextureWrite
{
	VkBuffer     srcBuffer  = VK_NULL_HANDLE;
	VkImage      dstImage   = VK_NULL_HANDLE;
	VkDeviceSize srcOffset  = 0;
	VkExtent3D   extent     = {};
	uint32_t     mipLevel   = 0;
	uint32_t     baseLayer  = 0;
	uint32_t     layerCount = 1;

	VkBufferImageCopy ToBufferImageCopy() const noexcept
	{
		VkBufferImageCopy copy{};
		copy.bufferOffset                    = srcOffset;
		copy.bufferRowLength                 = 0;
		copy.bufferImageHeight               = 0;
		copy.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		copy.imageSubresource.mipLevel       = mipLevel;
		copy.imageSubresource.baseArrayLayer = baseLayer;
		copy.imageSubresource.layerCount     = layerCount;
		copy.imageOffset                     = { 0, 0, 0 };
		copy.imageExtent                     = extent;
		return copy;
	}
};

struct PendingTextureUpload
{
	AllocatedImage*                  image  = nullptr;  // non-owning, points to caller's image
	std::vector<StagedTextureWrite>  writes = {};       // one per mip level
};

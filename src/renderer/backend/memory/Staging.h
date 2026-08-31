#pragma once

#include "VmaForward.h"
#include "AllocatedBuffer.h"
#include <atomic>
#include <span>

struct StagedTextureWrite;
struct AllocatedImage;
struct PendingTextureUpload;
struct TextureUploadDesc;
struct TextureMipDesc;

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

class Allocator;

class StagingBuffer final
{
	friend class Allocator;
protected:
	StagingBuffer()  = default;
	~StagingBuffer() = default;
	StagingBuffer(const StagingBuffer&)            = delete;
	StagingBuffer& operator=(const StagingBuffer&) = delete;

	void Init(size_t size, VmaAllocator vma, VkDevice device, size_t nonCoherentAtomSize);
	void Shutdown();

public:
	[[nodiscard]] StagedWrite          Stage(const void* data, size_t bytes, VkBuffer dst, VkDeviceSize dstOffset = 0);
	[[nodiscard]] PendingTextureUpload StageTexture(
		const void* data, size_t byteSize, AllocatedImage& image,
		std::span<const TextureMipDesc> mips = {});

	void CopyCommand       (VkCommandBuffer cmd, StagedWrite write) noexcept;
	void TextureCopyCommand(VkCommandBuffer cmd, const PendingTextureUpload& upload) const noexcept;
	void TextureCopyBatch  (VkCommandBuffer cmd, const std::vector<PendingTextureUpload>& batch) const noexcept;
	void ExecuteTextureBatch(VkCommandBuffer cmd, std::span<TextureUploadDesc> descs);

	void Flush() const;
	void FlushRange(size_t offset, size_t bytes) const;
	void Reset() { m_head.store(0, std::memory_order_release); }

	bool   CanFit        (size_t bytes, size_t alignment = 16) const noexcept;
	size_t GetCapacity   () const noexcept { return m_capacity; }
	size_t GetUsed       () const noexcept { return m_head.load(std::memory_order_relaxed); }
	float  GetUsageRatio () const noexcept { return static_cast<float>(GetUsed()) / static_cast<float>(m_capacity); }
	bool   IsValid       () const noexcept { return m_staging.IsValid(); }

private:
	size_t Suballocate(size_t bytes, size_t alignment);

	AllocatedBuffer     m_staging{};
	size_t              m_capacity = 0;
	size_t              m_atomSize = 1;
	std::atomic<size_t> m_head{0};

	VmaAllocator m_vma    = VK_NULL_HANDLE;
	VkDevice     m_device = VK_NULL_HANDLE;
};

#include "pch.h"

#include "Staging.h"
#include "Core.h"
#include <renderer/backend/memory/AllocatedImage.h>

void StagingBuffer::Init(size_t size, VmaAllocator vma, VkDevice device, size_t nonCoherentAtomSize)
{
	ASSERT(size > 0);
	ASSERT(vma != VK_NULL_HANDLE);
	ASSERT(device != VK_NULL_HANDLE);
	ASSERT(!m_staging.IsValid());

	m_staging.Reset();

	m_atomSize = nonCoherentAtomSize > 0 ? nonCoherentAtomSize : 1;
	m_vma      = vma;
	m_device   = device;

	VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.size        = size;
	bufferInfo.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo vmaInfo{};
	vmaInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
	vmaInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
					VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

	VmaAllocationInfo allocInfo{};
	VK_CHECK(vmaCreateBuffer(m_vma, &bufferInfo, &vmaInfo,
							 &m_staging.m_buffer,
							 &m_staging.m_allocation,
							 &allocInfo));

	m_staging.m_mappedPtr = allocInfo.pMappedData;
	m_staging.m_bytesSize = allocInfo.size;
	m_capacity            = allocInfo.size;
	m_head                = 0;

	ASSERT(m_staging.m_mappedPtr != nullptr && "Staging buffer must be persistently mapped");
}

void StagingBuffer::Shutdown()
{
	if (!m_staging.IsValid()) return;

	vmaDestroyBuffer(m_vma, m_staging.m_buffer, m_staging.m_allocation);
	m_staging.Reset();
	m_capacity = 0;
	m_head     = 0;
}

size_t StagingBuffer::Suballocate(size_t bytes, size_t alignment)
{
	const size_t offset = AlignUp(m_head, alignment);

	ASSERT(offset + bytes <= m_capacity);

	m_head = offset + bytes;
	return offset;
}

// --------
// BUFFERS
// --------

StagedWrite StagingBuffer::Stage(const void*  data,
								 size_t       bytes,
								 VkBuffer     dst,
								 VkDeviceSize dstOffset)
{
	ASSERT(data != nullptr);

	const size_t alignment = std::max(m_atomSize, static_cast<size_t>(16));
	const size_t offset    = Suballocate(bytes, alignment);

	memcpy(static_cast<uint8_t*>(m_staging.m_mappedPtr) + offset, data, bytes);

	StagedWrite write{};
	write.srcBuffer = m_staging.m_buffer;
	write.dstBuffer = dst;
	write.srcOffset = static_cast<VkDeviceSize>(offset);
	write.dstOffset = dstOffset;
	write.size      = static_cast<VkDeviceSize>(bytes);
	return write;
}

void StagingBuffer::CopyCommand(VkCommandBuffer cmd, StagedWrite write) noexcept
{
	VkBufferCopy copyBuffer = write.ToBufferCopy();
	vkCmdCopyBuffer(cmd, write.srcBuffer, write.dstBuffer, 1, &copyBuffer);
}


// ---------
// TEXTURES
// ---------

PendingTextureUpload StagingBuffer::StageTexture(const void* data, size_t pixelBytes, AllocatedImage& image)
{
	ASSERT(data != nullptr);

	const size_t width  = image.extent.width;
	const size_t height = image.extent.height;
	const size_t depth  = std::max(static_cast<uint32_t>(image.extent.depth), 1u);
	const size_t layers = image.bIsCubemap ? 6u : std::max(image.arrayLayers, 1u);
	const size_t bytes  = width * height * depth * layers * pixelBytes;

	const size_t alignment = std::max(m_atomSize, static_cast<size_t>(4));
	const size_t offset    = Suballocate(bytes, alignment);

	memcpy(static_cast<uint8_t*>(m_staging.m_mappedPtr) + offset, data, bytes);

	PendingTextureUpload pending{};
	pending.image = &image;
	pending.writes.emplace_back(StagedTextureWrite{
		.srcBuffer  = m_staging.m_buffer,
		.dstImage   = image.image,
		.srcOffset  = static_cast<VkDeviceSize>(offset),
		.extent     = image.extent,
		.mipLevel   = 0,
		.baseLayer  = 0,
		.layerCount = static_cast<uint32_t>(layers)
	});
	return pending;
}

PendingTextureUpload StagingBuffer::StageTextureMips(
	const std::vector<const void*>& mipDatas,
	const std::vector<VkExtent3D>&  mipExtents,
	size_t                          pixelBytes,
	AllocatedImage&                 image)
{
	ASSERT(!mipDatas.empty());
	ASSERT(mipDatas.size() == mipExtents.size());
	ASSERT(image.image != VK_NULL_HANDLE);

	const size_t layers    = image.bIsCubemap ? 6u : std::max(image.arrayLayers, 1u);
	const size_t alignment = std::max(m_atomSize, static_cast<size_t>(4));

	PendingTextureUpload pending{};
	pending.image = &image;
	pending.writes.reserve(mipDatas.size());

	for (uint32_t mip = 0; mip < static_cast<uint32_t>(mipDatas.size()); ++mip)
	{
		const VkExtent3D& ext   = mipExtents[mip];
		const size_t      bytes = static_cast<size_t>(ext.width)  *
								  static_cast<size_t>(ext.height) *
								  std::max(static_cast<size_t>(ext.depth), static_cast<size_t>(1)) *
								  layers * pixelBytes;

		ASSERT(mipDatas[mip] != nullptr);

		const size_t offset = Suballocate(bytes, alignment);
		memcpy(static_cast<uint8_t*>(m_staging.m_mappedPtr) + offset, mipDatas[mip], bytes);

		pending.writes.emplace_back(StagedTextureWrite{
			.srcBuffer  = m_staging.m_buffer,
			.dstImage   = image.image,
			.srcOffset  = static_cast<VkDeviceSize>(offset),
			.extent     = ext,
			.mipLevel   = mip,
			.baseLayer  = 0,
			.layerCount = static_cast<uint32_t>(layers)
		});
	}

	return pending;
}

void StagingBuffer::TextureCopyCommand(
	VkCommandBuffer cmd,
	const PendingTextureUpload& upload) const noexcept
{
	for (const auto& w : upload.writes)
	{
		VkBufferImageCopy copy = w.ToBufferImageCopy();
		vkCmdCopyBufferToImage(
			cmd,
			w.srcBuffer,
			w.dstImage,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&copy);
	}
}

void StagingBuffer::TextureCopyBatch(
	VkCommandBuffer cmd,
	const std::vector<PendingTextureUpload>& batch) const noexcept
{
	for (const auto& upload : batch)
		TextureCopyCommand(cmd, upload);
}


// ------
// FLUSH
// ------

void StagingBuffer::Flush() const
{
	if (m_head == 0) return;

	const size_t begin = 0;
	const size_t end   = AlignUp(m_head, m_atomSize);
	vmaFlushAllocation(m_vma, m_staging.m_allocation, begin, end);
}

void StagingBuffer::FlushRange(size_t offset, size_t bytes) const
{
	ASSERT(offset + bytes <= m_capacity);

	const size_t begin = offset & ~(m_atomSize - 1);
	const size_t end   = AlignUp(offset + bytes, m_atomSize);
	vmaFlushAllocation(m_vma, m_staging.m_allocation, begin, end - begin);
}

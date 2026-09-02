#include "pch.h"

#include "Staging.h"
#include "AllocatedImage.h"
#include "TextureStaging.h"
#include "../ImageUtils.h"

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

	m_head.store(0, std::memory_order_relaxed);

	ASSERT(m_staging.m_mappedPtr != nullptr);
}

void StagingBuffer::Shutdown()
{
	if (!m_staging.IsValid()) return;

	vmaDestroyBuffer(m_vma, m_staging.m_buffer, m_staging.m_allocation);
	m_staging.Reset();
	m_capacity = 0;
	m_head     = 0;
}

bool StagingBuffer::CanFit(size_t bytes, size_t alignment) const noexcept
{
	const size_t alignedSize = std::max(m_atomSize, alignment);
	const size_t current     = m_head.load(std::memory_order_relaxed);
	const size_t offset      = AllocatedBuffer::AlignUp(current, alignedSize);
	return (offset + bytes) <= m_capacity;
}

size_t StagingBuffer::Suballocate(size_t bytes, size_t alignment)
{
	const size_t alignedSize = std::max(m_atomSize, alignment);
	// Pad bytes up to alignment so the next allocation is also aligned
	const size_t paddedBytes = AllocatedBuffer::AlignUp(bytes, alignedSize);

	const size_t offset = m_head.fetch_add(paddedBytes, std::memory_order_acq_rel);
	const size_t end    = offset + bytes;

	ASSERT(end <= m_capacity);
	return offset;
}

StagedWrite StagingBuffer::Stage(const void* data, size_t bytes, VkBuffer dst, VkDeviceSize dstOffset)
{
	ASSERT(data != nullptr);
	ASSERT(dst != VK_NULL_HANDLE);

	const size_t alignment = std::max(m_atomSize, static_cast<size_t>(16u));
	const size_t offset    = Suballocate(bytes, alignment);

	std::memcpy(static_cast<uint8_t*>(m_staging.m_mappedPtr) + offset, data, bytes);

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
	VkBufferCopy copy = write.ToBufferCopy();
	vkCmdCopyBuffer(cmd, write.srcBuffer, write.dstBuffer, 1, &copy);
}

PendingTextureUpload StagingBuffer::StageTexture(
	const void* data, size_t byteSize, AllocatedImage& image,
	std::span<const TextureMipDesc> mips)
{
	ASSERT(data != nullptr);
	ASSERT(byteSize > 0);

	const size_t layers = image.m_bIsCubemap ? 6u : std::max(image.m_arrayLayers, 1u);

	const size_t alignment = std::max(m_atomSize, static_cast<size_t>(16u));
	const size_t offset = Suballocate(byteSize, alignment);

	std::memcpy(static_cast<uint8_t*>(m_staging.m_mappedPtr) + offset, data, byteSize);

	PendingTextureUpload pending{};
	pending.image = &image;

	if (mips.empty())
	{
		pending.writes.emplace_back(StagedTextureWrite{
			.srcBuffer = m_staging.m_buffer,
			.dstImage = image.m_image,
			.srcOffset = static_cast<VkDeviceSize>(offset),
			.extent = { image.m_extent.Width(), image.m_extent.Height(), image.m_extent.Depth() },
			.mipLevel = 0,
			.baseLayer = 0,
			.layerCount = static_cast<uint32_t>(layers)
			});
	}
	else
	{
		pending.writes.reserve(mips.size());

		for (uint32_t level = 0; level < static_cast<uint32_t>(mips.size()); ++level)
		{
			const TextureMipDesc& mip = mips[level];

			pending.writes.emplace_back(StagedTextureWrite{
				.srcBuffer = m_staging.m_buffer,
				.dstImage = image.m_image,
				.srcOffset = static_cast<VkDeviceSize>(offset + mip.offset),
				.extent = { mip.width, mip.height, 1u },
				.mipLevel = level,
				.baseLayer = 0,
				.layerCount = 1u
				});
		}
	}

	return pending;
}

void StagingBuffer::TextureCopyCommand(VkCommandBuffer cmd, const PendingTextureUpload& upload) const noexcept
{
	for (const auto& w : upload.writes)
	{
		VkBufferImageCopy copy = w.ToBufferImageCopy();
		vkCmdCopyBufferToImage(cmd, w.srcBuffer, w.dstImage,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
	}
}

void StagingBuffer::TextureCopyBatch(VkCommandBuffer cmd, const std::vector<PendingTextureUpload>& batch) const noexcept
{

	for (const auto& upload : batch)
		TextureCopyCommand(cmd, upload);
}

void StagingBuffer::ExecuteTextureBatch(VkCommandBuffer cmd, std::span<TextureUploadDesc> descs)
{
	ASSERT(!descs.empty());

	std::vector<PendingTextureUpload> uploads;
	uploads.reserve(descs.size());

	for (auto& desc : descs)
	{
		ASSERT(desc.IsValid());

		size_t bytes = desc.byteSize;
		if (bytes == 0)
		{
			const auto& img = *desc.image;
			const size_t layers = img.m_bIsCubemap ? 6u : std::max(img.m_arrayLayers, 1u);

			bytes = static_cast<size_t>(img.Width()) * img.Height()
				* std::max(img.Depth(), 1u) * layers * desc.pixelBytes;

			ASSERT(desc.pixelBytes > 0 &&
				"Block-compressed uploads must supply an explicit byteSize");
		}

		auto upload = StageTexture(desc.pixelData, bytes, *desc.image, desc.mips);
		upload.strategy = desc.strategy;
		upload.image->m_name = desc.image->m_name;

		uploads.emplace_back(std::move(upload));
	}

	Flush();

	std::vector<VkImageMemoryBarrier2> barriers;
	barriers.reserve(uploads.size());

	for (const auto& upload : uploads)
	{
		barriers.emplace_back(VkImageMemoryBarrier2{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
			.srcAccessMask = VK_ACCESS_2_NONE,
			.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.image = upload.image->m_image,
			.subresourceRange = {
				VK_IMAGE_ASPECT_COLOR_BIT,
				0, VK_REMAINING_MIP_LEVELS,
				0, VK_REMAINING_ARRAY_LAYERS
			}
			});
	}

	VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	dep.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
	dep.pImageMemoryBarriers = barriers.data();
	vkCmdPipelineBarrier2(cmd, &dep);

	TextureCopyBatch(cmd, uploads);

	for (const auto& upload : uploads)
	{
		if (upload.strategy == MipStrategy::GenerateOnGPU)
			ImageUtils::GenerateMipLevels(cmd, *upload.image);
		else
			if (upload.image->m_name != "DummyVelocity")
			{
				ImageUtils::TransitionLayout(cmd, *upload.image,
					RD::ImageAccess::TransferDst, RD::ImageAccess::Read);
			}
			else
			{
				ImageUtils::TransitionLayout(cmd, *upload.image,
					RD::ImageAccess::TransferDst, RD::ImageAccess::ComputeWrite);
			}
	}
}

void StagingBuffer::Flush() const
{
	const size_t used = m_head.load(std::memory_order_acquire);
	if (used == 0) return;

	const size_t end = AllocatedBuffer::AlignUp(used, m_atomSize);
	vmaFlushAllocation(m_vma, m_staging.m_allocation, 0, end);
}

void StagingBuffer::FlushRange(size_t offset, size_t bytes) const
{
	ASSERT(offset + bytes <= m_capacity);
	const size_t begin = offset & ~(m_atomSize - 1);
	const size_t end   = AllocatedBuffer::AlignUp(offset + bytes, m_atomSize);
	vmaFlushAllocation(m_vma, m_staging.m_allocation, begin, end - begin);
}

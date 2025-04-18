#include "pch.h"

#include "Texture.h"
#include "utils/RendererUtils.h"
#include "core/AssetManager.h"
#include "utils/VulkanUtils.h"
#include "vulkan/Backend.h"

std::optional<AllocatedImage> Textures::loadImage(fastgltf::Asset& asset, fastgltf::Image& image) {
	AllocatedImage newImage;

	int width, height, nrChannels;

	std::visit(fastgltf::visitor{
		[](auto& arg) {
			fmt::print("fastgltf::visitor fallback: unsupported image source {}\n", typeid(arg).name());
		},

		[&](fastgltf::sources::URI& filePath) {
			assert(filePath.fileByteOffset == 0); // We don't support offsets with stbi.
			assert(filePath.uri.isLocalPath());   // We're only capable of loading local files.

			std::filesystem::path relativePath(filePath.uri.path().begin(), filePath.uri.path().end());
			std::filesystem::path fullPath = AssetManager::getBasePath() / relativePath;

			unsigned char* data = stbi_load(fullPath.string().c_str(), &width, &height, &nrChannels, 4);

			if (data) {
				VkExtent3D imagesize;
				imagesize.width = width;
				imagesize.height = height;
				imagesize.depth = 1;

				newImage.imageExtent = imagesize;
				newImage.mipmapped = true;
				newImage.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;

				RendererUtils::createTextureImage(
					data,
					newImage,
					VK_IMAGE_USAGE_SAMPLED_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					VK_SAMPLE_COUNT_1_BIT,
					nullptr,
					AssetManager::getAssetAllocation()
				);

				//fmt::print("Image source is {}\n", image.data.index());

				stbi_image_free(data);
			}
			else {
				fmt::print("stbi_load failed for file: {}\n", fullPath.string());
			}
		},

		[&](fastgltf::sources::Array& array) {
			unsigned char* data = stbi_load_from_memory(
				reinterpret_cast<const unsigned char*>(array.bytes.data()),
				static_cast<int>(array.bytes.size()),
				&width, &height, &nrChannels, 4
			);

			if (data) {
				VkExtent3D imagesize;
				imagesize.width = width;
				imagesize.height = height;
				imagesize.depth = 1;

				newImage.imageExtent = imagesize;
				newImage.mipmapped = true;
				newImage.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;

				RendererUtils::createTextureImage(
					data,
					newImage,
					VK_IMAGE_USAGE_SAMPLED_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					VK_SAMPLE_COUNT_1_BIT,
					nullptr,
					AssetManager::getAssetAllocation()
				);

				//fmt::print("Image source is {}\n", image.data.index());

				stbi_image_free(data);
			}
			else {
				fmt::print("stbi_load_from_memory failed (Array source)\n");
			}
		},

		[&](fastgltf::sources::BufferView& view) {
			auto& bufferView = asset.bufferViews[view.bufferViewIndex];
			auto& buffer = asset.buffers[bufferView.bufferIndex];

			std::visit(fastgltf::visitor{

				[&](fastgltf::sources::Array& array) {
					unsigned char* data = stbi_load_from_memory(
						reinterpret_cast<const unsigned char*>(array.bytes.data()) + bufferView.byteOffset,
						static_cast<int>(bufferView.byteLength),
						&width, &height, &nrChannels, 4
					);

					if (data) {
						VkExtent3D imagesize = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
						newImage.imageExtent = imagesize;
						newImage.mipmapped = true;
						newImage.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;

						RendererUtils::createTextureImage(
							data,
							newImage,
							VK_IMAGE_USAGE_SAMPLED_BIT,
							VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
							VK_SAMPLE_COUNT_1_BIT,
							nullptr,
							AssetManager::getAssetAllocation()
						);

						stbi_image_free(data);
					}
					else {
						 fmt::print("stbi_load_from_memory failed (BufferView->Array)\n");
					}
				},


				[&](fastgltf::sources::URI& uri) {
					assert(uri.uri.isLocalPath());
					std::filesystem::path bufferPath = AssetManager::getBasePath() / std::string(uri.uri.path());
					std::ifstream file(bufferPath, std::ios::binary);

					if (!file) {
						fmt::print("Failed to open external buffer file: {}\n", bufferPath.string());
						return;
					}

					file.seekg(0, std::ios::end);
					size_t size = file.tellg();
					file.seekg(0, std::ios::beg);

					std::vector<uint8_t> dataBuf(size);
					file.read(reinterpret_cast<char*>(dataBuf.data()), size);

					unsigned char* data = stbi_load_from_memory(
						dataBuf.data() + bufferView.byteOffset,
						static_cast<int>(bufferView.byteLength),
						&width, &height, &nrChannels, 4
					);

					if (data) {
						VkExtent3D imagesize = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
						newImage.imageExtent = imagesize;
						newImage.mipmapped = true;
						newImage.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;

						RendererUtils::createTextureImage(
							data,
							newImage,
							VK_IMAGE_USAGE_SAMPLED_BIT,
							VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
							VK_SAMPLE_COUNT_1_BIT,
							nullptr,
							AssetManager::getAssetAllocation()
						);

						stbi_image_free(data);
					}
					else {
						fmt::print("stbi_load_from_memory failed for external buffer file: {}\n", bufferPath.string());
					}
				},
				[](auto& arg) {
					fmt::print("Unsupported buffer source inside BufferView: {}\n", typeid(arg).name());
				}
			}, buffer.data);
		}
	}, image.data);

	// If any of the attempts to load the data failed, we haven't written the image.
	if (newImage.image == VK_NULL_HANDLE) {
		return {};
	}
	else {
		return newImage;
	}
}

void Textures::generateMipmaps(VkCommandBuffer cmd, AllocatedImage& image) {
	uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(image.imageExtent.width, image.imageExtent.height)))) + 1;
	VkImage img = image.image;

	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.image = img;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;

	int32_t mipWidth = static_cast<int32_t>(image.imageExtent.width);
	int32_t mipHeight = static_cast<int32_t>(image.imageExtent.height);

	for (uint32_t i = 1; i < mipLevels; i++) {
		// Transition mip i - 1 to SRC
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);

		VkImageBlit blit{};
		blit.srcOffsets[0] = { 0, 0, 0 };
		blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 1;

		blit.dstOffsets[0] = { 0, 0, 0 };
		blit.dstOffsets[1] = { std::max(mipWidth / 2, 1), std::max(mipHeight / 2, 1), 1 };
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 1;

		vkCmdBlitImage(
			cmd,
			img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit,
			VK_FILTER_LINEAR
		);

		// Transition mip i - 1 to SHADER_READ_ONLY
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);

		mipWidth = std::max(mipWidth / 2, 1);
		mipHeight = std::max(mipHeight / 2, 1);
	}

	// Transition last mip level to SHADER_READ_ONLY
	barrier.subresourceRange.baseMipLevel = mipLevels - 1;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(
		cmd,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &barrier
	);
}

VkSampler Textures::createSampler(VkFilter filter, VkSamplerAddressMode addressMode, float maxAniso) {
	VkSamplerCreateInfo samplerInfo = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samplerInfo.magFilter = filter;
	samplerInfo.minFilter = filter;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.addressModeU = addressMode;
	samplerInfo.addressModeV = addressMode;
	samplerInfo.addressModeW = addressMode;
	samplerInfo.mipLodBias = 0.f;
	samplerInfo.anisotropyEnable = VK_TRUE;
	samplerInfo.maxAnisotropy = maxAniso;
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
	samplerInfo.minLod = 0.f;
	samplerInfo.maxLod = FLT_MAX;
	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;

	VkSampler sampler;
	VK_CHECK(vkCreateSampler(Backend::getDevice(), &samplerInfo, nullptr, &sampler));
	return sampler;
}
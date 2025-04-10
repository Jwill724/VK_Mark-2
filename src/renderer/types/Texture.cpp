#include "pch.h"

#include "Texture.h"
#include "utils/RendererUtils.h"
#include "core/AssetManager.h"
#include "utils/VulkanUtils.h"
#include "vulkan/Backend.h"

std::optional<AllocatedImage> Textures::loadImage(fastgltf::Asset& asset, fastgltf::Image& image) {
	AllocatedImage newImage{};

	int width, height, nrChannels;

	std::visit(fastgltf::visitor{
		[](auto& arg) {
			fmt::print("fastgltf::visitor fallback: unsupported image source {}\n", typeid(arg).name());
		},

		[&](fastgltf::sources::URI& filePath) {
			assert(filePath.fileByteOffset == 0); // We don't support offsets with stbi.
			assert(filePath.uri.isLocalPath());   // We're only capable of loading local files.

			const std::string path(filePath.uri.path().begin(), filePath.uri.path().end()); // Thanks C++.

			unsigned char* data = stbi_load(path.c_str(), &width, &height, &nrChannels, 4);

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

				stbi_image_free(data);
			}
			else {
				fmt::print("stbi_load failed for file: {}\n", path);
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
				[](auto& arg) {
					fmt::print("fastgltf::visitor fallback in BufferView: unsupported buffer source {}\n", typeid(arg).name());
				},

				[&](fastgltf::sources::Array& array) {
					unsigned char* data = stbi_load_from_memory(
						reinterpret_cast<const unsigned char*>(array.bytes.data()) + bufferView.byteOffset,
						static_cast<int>(bufferView.byteLength),
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

						stbi_image_free(data);
					}
					else {
						fmt::print("stbi_load_from_memory failed (BufferView->Array)\n");
					}
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

void Textures::generateMipmaps(VkCommandBuffer cmd, AllocatedImage image) {
	VkExtent2D imageSize = { image.imageExtent.width, image.imageExtent.height };

	// Check if image format supports linear blitting
	VkFormatProperties formatProperties;
	vkGetPhysicalDeviceFormatProperties(Backend::getPhysicalDevice(), image.imageFormat, &formatProperties);
	if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
		throw std::runtime_error("Texture image format does not support linear blitting!");
	}

	int mipLevels = int(std::floor(std::log2(std::max(imageSize.width, imageSize.height)))) + 1;
	for (int mip = 0; mip < mipLevels; mip++) {

		VkExtent2D halfSize = imageSize;
		halfSize.width /= 2;
		halfSize.height /= 2;

		halfSize.width = std::max(1u, halfSize.width);
		halfSize.height = std::max(1u, halfSize.height);

		VkImageMemoryBarrier2 imageBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .pNext = nullptr };

		imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
		imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;

		imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

		VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBarrier.subresourceRange = VulkanUtils::imageSubresourceRange(aspectMask);
		imageBarrier.subresourceRange.levelCount = 1;
		imageBarrier.subresourceRange.baseMipLevel = mip;
		imageBarrier.image = image.image;

		VkDependencyInfo depInfo{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .pNext = nullptr };
		depInfo.imageMemoryBarrierCount = 1;
		depInfo.pImageMemoryBarriers = &imageBarrier;

		vkCmdPipelineBarrier2(cmd, &depInfo);

		if (mip < mipLevels - 1) {
			VkImageBlit2 blitRegion{ .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2, .pNext = nullptr };

			blitRegion.srcOffsets[1].x = imageSize.width;
			blitRegion.srcOffsets[1].y = imageSize.height;
			blitRegion.srcOffsets[1].z = 1;

			blitRegion.dstOffsets[1].x = halfSize.width;
			blitRegion.dstOffsets[1].y = halfSize.height;
			blitRegion.dstOffsets[1].z = 1;

			blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blitRegion.srcSubresource.baseArrayLayer = 0;
			blitRegion.srcSubresource.layerCount = 1;
			blitRegion.srcSubresource.mipLevel = mip;

			blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blitRegion.dstSubresource.baseArrayLayer = 0;
			blitRegion.dstSubresource.layerCount = 1;
			blitRegion.dstSubresource.mipLevel = mip + 1;

			VkBlitImageInfo2 blitInfo{ .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2, .pNext = nullptr };
			blitInfo.dstImage = image.image;
			blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			blitInfo.srcImage = image.image;
			blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			blitInfo.filter = VK_FILTER_LINEAR;
			blitInfo.regionCount = 1;
			blitInfo.pRegions = &blitRegion;

			vkCmdBlitImage2(cmd, &blitInfo);

			imageSize = halfSize;
		}

		if (halfSize.width == 0 || halfSize.height == 0) {
			break;
		}
	}

	// transition all mip levels into the final read_only layout
	RendererUtils::transitionImage(cmd, image.image, image.imageFormat, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
#include "pch.h"

#include "TextureLoader.h"
#include "renderer/backend/Backend.h"
#include "utils/ImageUtils.h"
#include "engine/JobSystem.h"

std::optional<AllocatedImage> TextureLoader::loadImage(
	fastgltf::Asset& asset,
	fastgltf::Image& image,
	VkFormat format,
	ThreadContext& ctx,
	std::filesystem::path basePath,
	const VmaAllocator allocator,
	DeletionQueue& bufferQueue,
	const VkDevice device
) {
	AllocatedImage newImage;

	int width = 0, height = 0, nrChannels = 0;

	std::visit(fastgltf::visitor{
		[&](auto& arg) {
			JobSystem::log(ctx.threadID,
				fmt::format("[loadImage] fastgltf::visitor fallback: unsupported image source type: {}\n", typeid(arg).name()));
		},

		[&](fastgltf::sources::URI& filePath) {

			ASSERT(filePath.fileByteOffset == 0); // We don't support offsets with stbi.
			ASSERT(filePath.uri.isLocalPath());   // We're only capable of loading local files.

			std::filesystem::path relativePath(filePath.uri.path().begin(), filePath.uri.path().end());
			std::filesystem::path fullPath = basePath / relativePath;

			unsigned char* data = stbi_load(fullPath.string().c_str(), &width, &height, &nrChannels, 4);

			if (data && width > 0 && height > 0) {
				VkExtent3D imagesize{};
				imagesize.width = width;
				imagesize.height = height;
				imagesize.depth = 1;

				newImage.extent = imagesize;

				if (width >= 8 && height >= 8) {
					newImage.mipmapped = true;
				}
				else {
					newImage.mipmapped = false;
				}
				newImage.format = format;

				ImageUtils::createTextureImage(
					device,
					ctx.cmdPool,
					data,
					newImage,
					VK_IMAGE_USAGE_SAMPLED_BIT,
					VK_SAMPLE_COUNT_1_BIT,
					ctx.deletionQueue,
					bufferQueue,
					allocator,
					true
				);

				stbi_image_free(data);
			}
			else {
				JobSystem::log(ctx.threadID,
					fmt::format("[loadImage] stbi_load FAILED for file: {}\n", fullPath.string()));
			}
		},

		[&](fastgltf::sources::Array& array) {
			unsigned char* data = stbi_load_from_memory(
				reinterpret_cast<const unsigned char*>(array.bytes.data()),
				static_cast<int>(array.bytes.size()),
				&width, &height, &nrChannels, 4
			);

			if (data && width > 0 && height > 0) {
				VkExtent3D imagesize{};
				imagesize.width = width;
				imagesize.height = height;
				imagesize.depth = 1;

				newImage.extent = imagesize;

				if (width >= 8 && height >= 8) {
					newImage.mipmapped = true;
				}
				else {
					newImage.mipmapped = false;
				}
				newImage.format = format;

				ImageUtils::createTextureImage(
					device,
					ctx.cmdPool,
					data,
					newImage,
					VK_IMAGE_USAGE_SAMPLED_BIT,
					VK_SAMPLE_COUNT_1_BIT,
					ctx.deletionQueue,
					bufferQueue,
					allocator,
					true
				);

				stbi_image_free(data);
			}
			else {
				JobSystem::log(ctx.threadID,
					fmt::format("[loadImage] stbi_load_from_memory FAILED (Array source)\n"));
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

					if (data && width > 0 && height > 0) {

						VkExtent3D imagesize = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
						newImage.extent = imagesize;

						if (width >= 8 && height >= 8) {
							newImage.mipmapped = true;
						}
						else {
							newImage.mipmapped = false;
						}
						newImage.format = format;

						ImageUtils::createTextureImage(
							device,
							ctx.cmdPool,
							data,
							newImage,
							VK_IMAGE_USAGE_SAMPLED_BIT,
							VK_SAMPLE_COUNT_1_BIT,
							ctx.deletionQueue,
							bufferQueue,
							allocator,
							true
						);

						stbi_image_free(data);
					}
					else {
						JobSystem::log(ctx.threadID,
							fmt::format("[loadImage] stbi_load_from_memory FAILED (BufferView->Array)\n"));
					}
				},


				[&](fastgltf::sources::URI& uri) {
					ASSERT(uri.uri.isLocalPath());
					std::filesystem::path bufferPath = basePath / std::string(uri.uri.path());
					std::ifstream file(bufferPath, std::ios::binary);

					if (!file) {
						JobSystem::log(ctx.threadID,
							fmt::format("[loadImage] Failed to open external buffer file: {}\n", bufferPath.string()));
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

					if (data && width > 0 && height > 0) {
						VkExtent3D imagesize = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
						newImage.extent = imagesize;

						if (width >= 8 && height >= 8) {
							newImage.mipmapped = true;
						}
						else {
							newImage.mipmapped = false;
						}
						newImage.format = format;

						ImageUtils::createTextureImage(
							device,
							ctx.cmdPool,
							data,
							newImage,
							VK_IMAGE_USAGE_SAMPLED_BIT,
							VK_SAMPLE_COUNT_1_BIT,
							ctx.deletionQueue,
							bufferQueue,
							allocator,
							true
						);

						stbi_image_free(data);

					}
					else {
						JobSystem::log(ctx.threadID,
							fmt::format("[loadImage] stbi_load_from_memory FAILED for external buffer file: {}\n", bufferPath.string()));
					}
				},
				[&](auto& arg) {
					JobSystem::log(ctx.threadID,
						fmt::format("[loadImage] Unsupported buffer source inside BufferView: {}\n", typeid(arg).name()));
				}
			}, buffer.data);
		}
	}, image.data);

	// If any of the attempts to load the data failed, we haven't written the image.
	if (newImage.image == VK_NULL_HANDLE) {
		JobSystem::log(ctx.threadID, fmt::format("[loadImage] FAILED: No valid image allocated for '{}'\n", image.name));
		return {};
	}
	else {
		//JobSystem::log(ctx.threadID, fmt::format("[loadImage] SUCCESS: Allocated image for '{}'\n", image.name));
		return newImage;
	}
}

VkFilter TextureLoader::extract_filter(fastgltf::Filter filter) {
	switch (filter) {
		// nearest samplers
	case fastgltf::Filter::Nearest:
	case fastgltf::Filter::NearestMipMapNearest:
	case fastgltf::Filter::NearestMipMapLinear:
		return VK_FILTER_NEAREST;

		// linear samplers
	case fastgltf::Filter::Linear:
	case fastgltf::Filter::LinearMipMapNearest:
	case fastgltf::Filter::LinearMipMapLinear:
	default:
		return VK_FILTER_LINEAR;
	}
}
VkSamplerMipmapMode TextureLoader::extract_mipmap_mode(fastgltf::Filter filter) {
	switch (filter) {
	case fastgltf::Filter::NearestMipMapNearest:
	case fastgltf::Filter::LinearMipMapNearest:
		return VK_SAMPLER_MIPMAP_MODE_NEAREST;

	case fastgltf::Filter::NearestMipMapLinear:
	case fastgltf::Filter::LinearMipMapLinear:
	default:
		return VK_SAMPLER_MIPMAP_MODE_LINEAR;
	}
}
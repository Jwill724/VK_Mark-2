#include "pch.h"

#include "TextureLoader.h"
#include "renderer/backend/Backend.h"
#include "utils/ImageUtils.h"

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

	//fmt::print("[loadImage] Start loading image: '{}'\n", image.name);

	std::visit(fastgltf::visitor{
		[](auto& arg) {
			fmt::print("[loadImage] fastgltf::visitor fallback: unsupported image source type: {}\n", typeid(arg).name());
		},

		[&](fastgltf::sources::URI& filePath) {
			//fmt::print("[loadImage] URI source detected\n");

			ASSERT(filePath.fileByteOffset == 0); // We don't support offsets with stbi.
			ASSERT(filePath.uri.isLocalPath());   // We're only capable of loading local files.

			std::filesystem::path relativePath(filePath.uri.path().begin(), filePath.uri.path().end());
			std::filesystem::path fullPath = basePath / relativePath;

			//fmt::print("[loadImage] Loading from file: {}\n", fullPath.string());

			unsigned char* data = stbi_load(fullPath.string().c_str(), &width, &height, &nrChannels, 4);

			if (data && width > 0 && height > 0) {
				//fmt::print("[loadImage] Success: {}x{} Channels:{}\n", width, height, nrChannels);

				VkExtent3D imagesize{};
				imagesize.width = width;
				imagesize.height = height;
				imagesize.depth = 1;

				newImage.imageExtent = imagesize;

				if (width >= 8 && height >= 8) {
					newImage.mipmapped = true;
				}
				else {
					newImage.mipmapped = false;
				}
				newImage.imageFormat = format;

				//fmt::print("[createTextureImage] Allocating image: {}x{} mipmapped:{}\n",
				//	imagesize.width, imagesize.height, newImage.mipmapped);

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

				//fmt::print("[createTextureImage] Allocation done: Image handle {}\n", (void*)newImage.image);


				//fmt::print("Image source is {}\n", image.data.index());

				stbi_image_free(data);
			}
			else {
				fmt::print("[loadImage] stbi_load FAILED for file: {}\n", fullPath.string());
			}
		},

		[&](fastgltf::sources::Array& array) {
			//fmt::print("[loadImage] Array source detected\n");

			unsigned char* data = stbi_load_from_memory(
				reinterpret_cast<const unsigned char*>(array.bytes.data()),
				static_cast<int>(array.bytes.size()),
				&width, &height, &nrChannels, 4
			);

			if (data && width > 0 && height > 0) {
				//fmt::print("[loadImage] Success: {}x{} Channels:{}\n", width, height, nrChannels);
				VkExtent3D imagesize{};
				imagesize.width = width;
				imagesize.height = height;
				imagesize.depth = 1;

				newImage.imageExtent = imagesize;

				if (width >= 8 && height >= 8) {
					newImage.mipmapped = true;
				}
				else {
					newImage.mipmapped = false;
				}
				newImage.imageFormat = format;

				//fmt::print("[createTextureImage] Allocating image: {}x{} mipmapped:{}\n",
				//	imagesize.width, imagesize.height, newImage.mipmapped);

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

				//fmt::print("[createTextureImage] Allocation done: Image handle {}\n", (void*)newImage.image);


				//fmt::print("Image source is {}\n", image.data.index());

				stbi_image_free(data);
			}
			else {
				fmt::print("[loadImage] stbi_load_from_memory FAILED (Array source)\n");
			}
		},

		[&](fastgltf::sources::BufferView& view) {
			//fmt::print("[loadImage] BufferView source detected\n");

			auto& bufferView = asset.bufferViews[view.bufferViewIndex];
			auto& buffer = asset.buffers[bufferView.bufferIndex];

			std::visit(fastgltf::visitor{

				[&](fastgltf::sources::Array& array) {
					//fmt::print("[loadImage] BufferView->Array detected\n");

					unsigned char* data = stbi_load_from_memory(
						reinterpret_cast<const unsigned char*>(array.bytes.data()) + bufferView.byteOffset,
						static_cast<int>(bufferView.byteLength),
						&width, &height, &nrChannels, 4
					);

					if (data && width > 0 && height > 0) {
						//fmt::print("[loadImage] Success: {}x{} Channels:{}\n", width, height, nrChannels);

						VkExtent3D imagesize = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
						newImage.imageExtent = imagesize;

						if (width >= 8 && height >= 8) {
							newImage.mipmapped = true;
						}
						else {
							newImage.mipmapped = false;
						}
						newImage.imageFormat = format;

						//fmt::print("[createTextureImage] Allocating image: {}x{} mipmapped:{}\n",
						//	imagesize.width, imagesize.height, newImage.mipmapped);

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

						//fmt::print("[createTextureImage] Allocation done: Image handle {}\n", (void*)newImage.image);

						stbi_image_free(data);
					}
					else {
						 fmt::print("[loadImage] stbi_load_from_memory FAILED (BufferView->Array)\n");
					}
				},


				[&](fastgltf::sources::URI& uri) {
					//fmt::print("[loadImage] BufferView->URI detected\n");

					ASSERT(uri.uri.isLocalPath());
					std::filesystem::path bufferPath = basePath / std::string(uri.uri.path());
					std::ifstream file(bufferPath, std::ios::binary);

					if (!file) {
						fmt::print("[loadImage] Failed to open external buffer file: {}\n", bufferPath.string());
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
						//fmt::print("[loadImage] Success: {}x{} Channels:{}\n", width, height, nrChannels);

						VkExtent3D imagesize = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
						newImage.imageExtent = imagesize;

						if (width >= 8 && height >= 8) {
							newImage.mipmapped = true;
						}
						else {
							newImage.mipmapped = false;
						}
						newImage.imageFormat = format;

						//fmt::print("[createTextureImage] Allocating image: {}x{} mipmapped:{}\n",
						//	imagesize.width, imagesize.height, newImage.mipmapped);

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

						//fmt::print("[createTextureImage] Allocation done: Image handle {}\n", (void*)newImage.image);

						stbi_image_free(data);

					}
					else {
						fmt::print("[loadImage] stbi_load_from_memory FAILED for external buffer file: {}\n", bufferPath.string());
					}
				},
				[](auto& arg) {
					fmt::print("[loadImage] Unsupported buffer source inside BufferView: {}\n", typeid(arg).name());
				}
			}, buffer.data);
		}
	}, image.data);

	// If any of the attempts to load the data failed, we haven't written the image.
	if (newImage.image == VK_NULL_HANDLE) {
		fmt::print("[loadImage] FAILED: No valid image allocated for '{}'\n", image.name);
		return {};
	}
	else {
		//fmt::print("[loadImage] SUCCESS: Allocated image for '{}'\n", image.name);
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
#include "pch.h"

#include "AssetManager.h"

#include "Engine.h"
#include "vulkan/Backend.h"

#include "utils/BufferUtils.h"
#include "utils/VulkanUtils.h"
#include "utils/RendererUtils.h"
#include "renderer/RenderScene.h"
#include "renderer/CommandBuffer.h"

namespace AssetManager {
	std::vector<std::shared_ptr<MeshAsset>> testMeshes;
	std::vector<std::shared_ptr<MeshAsset>>& getTestMeshes() { return testMeshes; }

	GPUMeshBuffers rectangle;

	// Textures
	AllocatedImage _whiteImage;
	AllocatedImage& getWhiteImage() { return _whiteImage; }

	AllocatedImage _blackImage;
	AllocatedImage _greyImage;
	AllocatedImage _errorCheckerboardImage;
	AllocatedImage& getCheckboardTex() { return _errorCheckerboardImage; }

	std::vector<AllocatedImage> texImages;
	std::vector<AllocatedImage>& getTexImages() { return texImages; }

	VkSampler _defaultSamplerLinear;
	VkSampler _defaultSamplerNearest;
	VkSampler getDefaultSamplerLinear() { return _defaultSamplerLinear; }
	VkSampler getDefaultSamplerNearest() { return _defaultSamplerNearest; }

	void initTextures();
	DeletionQueue _assetDeletionQueue;
	DeletionQueue& getAssetDeletionQueue() { return _assetDeletionQueue; }
	VmaAllocator _assetAllocator;
	VmaAllocator& getAssetAllocation() { return _assetAllocator; }

	// for asset setup
	// For now only area of engine to have this just for asset loading
	ImmCmdSubmitDef _immGraphicsCmdSubmit{};
	ImmCmdSubmitDef& getGraphicsCmdSubmit() { return _immGraphicsCmdSubmit; }

	ImmCmdSubmitDef _immTransferCmdSubmit{};
}

// all backend needs to call
void AssetManager::loadAssets() {
	_assetAllocator = VulkanUtils::createAllocator(Backend::getPhysicalDevice(), Backend::getDevice(), Backend::getInstance());
	_assetDeletionQueue.push_function([=] {
		vmaDestroyAllocator(_assetAllocator);
	});

	// single command submit for copying and moving asset data
	CommandBuffer::setupImmediateCmdBuffer(_immGraphicsCmdSubmit, Backend::getGraphicsQueue());

	// only meant for pre rendering copies to gpu
	CommandBuffer::setupImmediateCmdBuffer(_immTransferCmdSubmit, Backend::getTransferQueue());

	testMeshes = loadGltfMeshes("res/models/basicmesh.glb").value();

	initTextures();
}

// TEXTURES
void AssetManager::initTextures() {
	// reuse for now
	VkExtent3D texExtent = { 1, 1, 1 };

	VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
	VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	VkMemoryPropertyFlags memoryProp = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;

	_whiteImage.imageExtent = texExtent;
	_whiteImage.imageFormat = format;
	_whiteImage.mipmapped = false;

	_greyImage.imageExtent = texExtent;
	_greyImage.imageFormat = format;
	_greyImage.mipmapped = false;

	_blackImage.imageExtent = texExtent;
	_blackImage.imageFormat = format;
	_blackImage.mipmapped = false;

	//3 default textures, white, grey, black. 1 pixel each
	// all same settings
	uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
	RendererUtils::createTextureImage((void*)&white, _whiteImage, usage, memoryProp, samples, _assetDeletionQueue, _assetAllocator);
	texImages.push_back(_whiteImage);

	uint32_t grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
	RendererUtils::createTextureImage((void*)&grey, _greyImage, usage, memoryProp, samples, _assetDeletionQueue, _assetAllocator);
	texImages.push_back(_greyImage);

	uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 1));
	RendererUtils::createTextureImage((void*)&black, _blackImage, usage, memoryProp, samples, _assetDeletionQueue, _assetAllocator);
	texImages.push_back(_blackImage);

	//checkerboard image
	uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
	std::array<uint32_t, 16 * 16 > pixels; //for 16x16 checkerboard texture
	for (int x = 0; x < 16; x++) {
		for (int y = 0; y < 16; y++) {
			pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
		}
	}

	VkExtent3D checkerboardedImageExtent { 16, 16, 1 };

	_errorCheckerboardImage.imageExtent = checkerboardedImageExtent;
	_errorCheckerboardImage.imageFormat = format;
	_errorCheckerboardImage.mipmapped = false;

	RendererUtils::createTextureImage(pixels.data(), _errorCheckerboardImage, usage, memoryProp, samples, _assetDeletionQueue, _assetAllocator);
	texImages.push_back(_errorCheckerboardImage);

	VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

	sampl.magFilter = VK_FILTER_NEAREST;
	sampl.minFilter = VK_FILTER_NEAREST;

	vkCreateSampler(Backend::getDevice(), &sampl, nullptr, &_defaultSamplerNearest);

	sampl.magFilter = VK_FILTER_LINEAR;
	sampl.minFilter = VK_FILTER_LINEAR;

	vkCreateSampler(Backend::getDevice(), &sampl, nullptr, &_defaultSamplerLinear);

	_assetDeletionQueue.push_function([&]() {
		vkDestroySampler(Backend::getDevice(), _defaultSamplerNearest, nullptr);
		vkDestroySampler(Backend::getDevice(), _defaultSamplerLinear, nullptr);
	});
}

std::optional<std::vector<std::shared_ptr<MeshAsset>>> AssetManager::loadGltfMeshes(const std::filesystem::path& filePath) {
	std::cout << "Loading GLTF: " << filePath << std::endl;

	// OPEN MESHES
	fastgltf::Parser parser;

	// Reads the .glb/.gltf into memory
	auto data = fastgltf::GltfDataBuffer::FromPath(filePath);
	if (data.error() != fastgltf::Error::None) {
		std::cerr << "Failed to load file: " << static_cast<int>(data.error()) << '\n';
		return std::nullopt;
	}

	// Parse it
	auto asset = parser.loadGltf(data.get(), filePath.parent_path(), fastgltf::Options::None);
	if (auto error = asset.error(); error != fastgltf::Error::None) {
		std::cerr << "GLTF parse error: " << static_cast<int>(error) << '\n';
		return std::nullopt;
	}

	// LOAD MESHES
	std::vector<std::shared_ptr<MeshAsset>> meshes;

	// use the same vectors for all meshes so that the memory doesn't reallocate as often
	std::vector<uint32_t> indices;
	std::vector<Vertex> vertices;
	for (fastgltf::Mesh& mesh : asset->meshes) {
		MeshAsset newmesh;

		newmesh.name = mesh.name;

		// clear the mesh arrays each mesh, we don't want to merge them by error
		indices.clear();
		vertices.clear();

		for (auto&& p : mesh.primitives) {
			GeoSurface newSurface;
			newSurface.startIndex = (uint32_t)indices.size();
			newSurface.count = (uint32_t)asset->accessors[p.indicesAccessor.value()].count;

			size_t initial_vtx = vertices.size();

			// load indexes
			fastgltf::Accessor& indexaccessor = asset->accessors[p.indicesAccessor.value()];
			indices.reserve(indices.size() + indexaccessor.count);

			fastgltf::iterateAccessorWithIndex<std::uint32_t>(asset.get(), indexaccessor,
				[&](uint32_t idx, size_t /*index*/) {
					indices.push_back(static_cast<uint32_t>(idx + initial_vtx));
				});

			// load vertex positions
			fastgltf::Accessor& posAccessor = asset->accessors[p.findAttribute("POSITION")->accessorIndex];
			vertices.resize(vertices.size() + posAccessor.count);

			fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(), posAccessor,
				[&](glm::vec3 v, size_t index) {
					Vertex newvtx;
					newvtx.position = v;
					newvtx.normal = { 1, 0, 0 };
					newvtx.color = glm::vec4{ 1.f };
					newvtx.uv_x = 0;
					newvtx.uv_y = 0;
					vertices[initial_vtx + index] = newvtx;
				});


			// load vertex normals
			auto normals = p.findAttribute("NORMAL");
			if (normals != p.attributes.end()) {

				fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(), asset.get().accessors[(*normals).accessorIndex],
					[&](glm::vec3 v, size_t index) {
						vertices[initial_vtx + index].normal = v;
					});
			}

			// load UVs
			auto uv = p.findAttribute("TEXCOORD_0");
			if (uv != p.attributes.end()) {

				fastgltf::iterateAccessorWithIndex<glm::vec2>(asset.get(), asset.get().accessors[(*uv).accessorIndex],
					[&](glm::vec2 v, size_t index) {
						vertices[initial_vtx + index].uv_x = v.x;
						vertices[initial_vtx + index].uv_y = v.y;
					});
			}

			// load vertex colors
			auto colors = p.findAttribute("COLOR_0");
			if (colors != p.attributes.end()) {

				fastgltf::iterateAccessorWithIndex<glm::vec4>(asset.get(), asset->accessors[(*colors).accessorIndex],
					[&](glm::vec4 v, size_t index) {
						vertices[initial_vtx + index].color = v;
					});
			}
			newmesh.surfaces.push_back(newSurface);
		}

		// display the vertex normals
		constexpr bool OverrideColors = false;
		if (OverrideColors) {
			for (Vertex& vtx : vertices) {
				vtx.color = glm::vec4(vtx.normal, 1.f);
			}
		}
		newmesh.meshBuffers = uploadMesh(indices, vertices);

		meshes.emplace_back(std::make_shared<MeshAsset>(std::move(newmesh)));

		_assetDeletionQueue.push_function([=] {
			BufferUtils::destroyBuffer(meshes.back()->meshBuffers.indexBuffer, _assetAllocator);
			BufferUtils::destroyBuffer(meshes.back()->meshBuffers.vertexBuffer, _assetAllocator);
		});
	}

	return meshes;
}

GPUMeshBuffers AssetManager::uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices) {
	const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
	const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

	GPUMeshBuffers newSurface;

	//create vertex buffer
	newSurface.vertexBuffer = BufferUtils::createBuffer(vertexBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY, _assetAllocator);

	//find the address of the vertex buffer
	VkBufferDeviceAddressInfo deviceAdressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = newSurface.vertexBuffer.buffer };
	newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(Backend::getDevice(), &deviceAdressInfo);

	//create index buffer
	newSurface.indexBuffer = BufferUtils::createBuffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY, _assetAllocator);

	AllocatedBuffer staging = BufferUtils::createBuffer(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, _assetAllocator);

	void* data;
	vmaMapMemory(_assetAllocator, staging.allocation, &data);

	// copy vertex buffer
	memcpy(data, vertices.data(), vertexBufferSize);
	// copy index buffer
	memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

	vmaUnmapMemory(_assetAllocator, staging.allocation);

	// Note that this pattern is not very efficient,
	// as we are waiting for the GPU command to fully execute before continuing with our CPU side logic.
	//
	// This is something people generally put on a background thread,
	// whose sole job is to execute uploads like this one, and deleting / reusing the staging buffers.

	CommandBuffer::immediateCmdSubmit([&](VkCommandBuffer cmd) {
		VkBufferCopy vertexCopy{ 0 };
		vertexCopy.dstOffset = 0;
		vertexCopy.srcOffset = 0;
		vertexCopy.size = vertexBufferSize;

		vkCmdCopyBuffer(cmd, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy);

		VkBufferCopy indexCopy{ 0 };
		indexCopy.dstOffset = 0;
		indexCopy.srcOffset = vertexBufferSize;
		indexCopy.size = indexBufferSize;

		vkCmdCopyBuffer(cmd, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy);
	},
	_immTransferCmdSubmit,
	Backend::getTransferQueue()
	);

	BufferUtils::destroyBuffer(staging, _assetAllocator);

	return newSurface;
}
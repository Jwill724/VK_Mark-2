#include "pch.h"

#include "AssetManager.h"

#include "Engine.h"
#include "vulkan/Backend.h"

#include "utils/BufferUtils.h"
#include "utils/VulkanUtils.h"
#include "utils/RendererUtils.h"
#include "renderer/RenderScene.h"
#include "renderer/CommandBuffer.h"
#include "renderer/Descriptor.h"
#include "renderer/types/Texture.h"

namespace AssetManager {
	std::vector<std::shared_ptr<MeshAsset>> testMeshes;
	std::vector<std::shared_ptr<MeshAsset>>& getTestMeshes() { return testMeshes; }

	// Textures
	AllocatedImage _whiteImage;
	AllocatedImage& getWhiteImage() { return _whiteImage; }

	AllocatedImage _blackImage;
	AllocatedImage _greyImage;
	AllocatedImage _errorCheckerboardImage;
	AllocatedImage& getCheckboardTex() { return _errorCheckerboardImage; }

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

// all that is needed to call in engine
void AssetManager::loadAssets() {
	_assetAllocator = VulkanUtils::createAllocator(Backend::getPhysicalDevice(), Backend::getDevice(), Backend::getInstance());
	_assetDeletionQueue.push_function([=] {
		vmaDestroyAllocator(_assetAllocator);
	});

	// single command submit for copying and moving asset data
	CommandBuffer::setupImmediateCmdBuffer(_immGraphicsCmdSubmit, Backend::getGraphicsQueue());

	// only meant for pre rendering copies to gpu
	CommandBuffer::setupImmediateCmdBuffer(_immTransferCmdSubmit, Backend::getTransferQueue());

	initTextures();

	std::string structurePath = { "res/models/structure_mat.glb" };
	auto structureFile = loadGltf(structurePath);

	assert(structureFile.has_value());

	RenderScene::loadedScenes["structure"] = *structureFile;
}

// Global helper functions
// Texture sampling, probably move somewhere else
static VkFilter extract_filter(fastgltf::Filter filter) {
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
static VkSamplerMipmapMode extract_mipmap_mode(fastgltf::Filter filter) {
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
	_whiteImage.mipmapped = true;

	_greyImage.imageExtent = texExtent;
	_greyImage.imageFormat = format;
	_greyImage.mipmapped = true;

	_blackImage.imageExtent = texExtent;
	_blackImage.imageFormat = format;
	_blackImage.mipmapped = true;

	//3 default textures, white, grey, black. 1 pixel each
	// all same settings
	uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
	RendererUtils::createTextureImage((void*)&white, _whiteImage, usage, memoryProp, samples, &_assetDeletionQueue, _assetAllocator);

	uint32_t grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
	RendererUtils::createTextureImage((void*)&grey, _greyImage, usage, memoryProp, samples, &_assetDeletionQueue, _assetAllocator);

	uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 1));
	RendererUtils::createTextureImage((void*)&black, _blackImage, usage, memoryProp, samples, &_assetDeletionQueue, _assetAllocator);

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
	_errorCheckerboardImage.mipmapped = true;

	RendererUtils::createTextureImage(pixels.data(), _errorCheckerboardImage, usage, memoryProp, samples, &_assetDeletionQueue, _assetAllocator);

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

std::optional<std::shared_ptr<LoadedGLTF>> AssetManager::loadGltf(std::string_view filePath) {
	fmt::print("Loading GLTF: {}\n", filePath);

	auto scene = std::make_shared<LoadedGLTF>();
	LoadedGLTF& file = *scene.get();

	std::filesystem::path path = filePath;
	fastgltf::Parser parser;

	// Load the glTF file into memory using the modern API
	auto data = fastgltf::GltfDataBuffer::FromPath(path);
	if (!data || data.error() != fastgltf::Error::None) {
		fmt::print("Failed to load file: error code {}\n", static_cast<int>(data.error()));
		return std::nullopt;
	}

	constexpr auto gltfOptions =
		fastgltf::Options::DontRequireValidAssetMember |
		fastgltf::Options::AllowDouble |
		fastgltf::Options::LoadGLBBuffers |
		fastgltf::Options::LoadExternalBuffers |
		fastgltf::Options::LoadExternalImages;

	auto type = fastgltf::determineGltfFileType(data.get());
	fastgltf::Asset gltf;

	if (type == fastgltf::GltfType::glTF) {
		auto result = parser.loadGltfBinary(data.get(), path.parent_path(), gltfOptions);
		if (!result || result.error() != fastgltf::Error::None) {
			fmt::print("Failed to parse .gltf: error code {}\n", static_cast<int>(result.error()));
			return std::nullopt;
		}
		gltf = std::move(result.get());
	}
	else if (type == fastgltf::GltfType::GLB) {
		auto result = parser.loadGltfBinary(data.get(), path.parent_path(), gltfOptions);
		if (!result || result.error() != fastgltf::Error::None) {
			fmt::print("Failed to parse .glb: error code {}\n", static_cast<int>(result.error()));
			return std::nullopt;
		}
		gltf = std::move(result.get());
	}
	else {
		fmt::print("Unknown or unsupported glTF file type\n");
		return std::nullopt;
	}


	DescriptorSetOverwatch::initAssetDescriptors(gltf.materials.size());


	for (fastgltf::Sampler& sampler : gltf.samplers) {

		VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .pNext = nullptr };
		sampl.maxLod = VK_LOD_CLAMP_NONE;
		sampl.minLod = 0;

		sampl.magFilter = extract_filter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
		sampl.minFilter = extract_filter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

		sampl.mipmapMode = extract_mipmap_mode(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

		VkSampler newSampler;
		vkCreateSampler(Backend::getDevice(), &sampl, nullptr, &newSampler);

		file.samplers.push_back(newSampler);
	}

	std::vector<std::shared_ptr<MeshAsset>> meshes;
	std::vector<std::shared_ptr<Node>> nodes;
	std::vector<AllocatedImage> images;
	std::vector<std::shared_ptr<GLTFMaterial>> materials;

	// MeshNodes depend on meshes, meshes depend on materials, and materials on textures

	// load all textures
	for (fastgltf::Image& image : gltf.images) {
		std::optional<AllocatedImage> img = Textures::loadImage(gltf, image);

		if (img.has_value()) {
			images.push_back(*img);
			file.images[image.name.c_str()] = *img;
			fmt::print("Loaded texture {}\n", image.name);
		}
		else {
			// we failed to load, so lets give the slot a default white texture to not
			// completely break loading
			images.push_back(_errorCheckerboardImage);
			fmt::print("gltf failed to load texture {}\n", image.name);
		}
	}

	// create buffer to hold the material data
	file.materialDataBuffer = BufferUtils::createBuffer(sizeof(GLTFMetallic_Roughness::MaterialConstants) * gltf.materials.size(),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, _assetAllocator);
	int data_index = 0;
	GLTFMetallic_Roughness::MaterialConstants* sceneMaterialConstants =
		(GLTFMetallic_Roughness::MaterialConstants*)file.materialDataBuffer.info.pMappedData;

	for (fastgltf::Material& mat : gltf.materials) {
		auto newMat = std::make_shared<GLTFMaterial>();
		materials.push_back(newMat);
		file.materials[mat.name.c_str()] = newMat;

		GLTFMetallic_Roughness::MaterialConstants constants;
		constants.colorFactors.x = mat.pbrData.baseColorFactor[0];
		constants.colorFactors.y = mat.pbrData.baseColorFactor[1];
		constants.colorFactors.z = mat.pbrData.baseColorFactor[2];
		constants.colorFactors.w = mat.pbrData.baseColorFactor[3];

		constants.metal_rough_factors.x = mat.pbrData.metallicFactor;
		constants.metal_rough_factors.y = mat.pbrData.roughnessFactor;
		// write material parameters to buffer
		sceneMaterialConstants[data_index] = constants;

		MaterialPass passType = MaterialPass::MainColor;
		if (mat.alphaMode == fastgltf::AlphaMode::Blend) {
			passType = MaterialPass::Transparent;
		}

		GLTFMetallic_Roughness::MaterialResources materialResources;
		// default the material textures
		materialResources.colorImage = _whiteImage;
		materialResources.colorSampler = _defaultSamplerLinear;
		materialResources.metalRoughImage = _whiteImage;
		materialResources.metalRoughSampler = _defaultSamplerLinear;

		// set the uniform buffer for the material data
		materialResources.dataBuffer = file.materialDataBuffer.buffer;
		materialResources.dataBufferOffset = data_index * sizeof(GLTFMetallic_Roughness::MaterialConstants);
		// grab textures from gltf file
		if (mat.pbrData.baseColorTexture.has_value()) {
			size_t img = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].imageIndex.value();
			size_t sampler = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].samplerIndex.value();

			materialResources.colorImage = images[img];
			materialResources.colorSampler = file.samplers[sampler];
		}
		// build material
		newMat->data = RenderScene::metalRoughMaterial.writeMaterial(
			Backend::getDevice(),
			passType,
			materialResources,
			DescriptorSetOverwatch::assetDescriptorManager
		);

		data_index++;
	}

	// use the same vectors for all meshes so that the memory doesnt reallocate as often
	std::vector<uint32_t> indices;
	std::vector<Vertex> vertices;

	for (fastgltf::Mesh& mesh : gltf.meshes) {
		std::shared_ptr<MeshAsset> newmesh = std::make_shared<MeshAsset>();
		meshes.push_back(newmesh);
		file.meshes[mesh.name.c_str()] = newmesh;
		newmesh->name = mesh.name;

		// clear the mesh arrays each mesh, we dont want to merge them by error
		indices.clear();
		vertices.clear();

		for (auto&& p : mesh.primitives) {
			GeoSurface newSurface;
			newSurface.startIndex = (uint32_t)indices.size();
			newSurface.count = (uint32_t)gltf.accessors[p.indicesAccessor.value()].count;

			size_t initial_vtx = vertices.size();

			// load indexes
			fastgltf::Accessor& indexaccessor = gltf.accessors[p.indicesAccessor.value()];
			indices.reserve(indices.size() + indexaccessor.count);

			fastgltf::iterateAccessorWithIndex<std::uint32_t>(gltf, indexaccessor,
				[&](uint32_t idx, size_t /*index*/) {
					indices.push_back(static_cast<uint32_t>(idx + initial_vtx));
				});

			// load vertex positions
			fastgltf::Accessor& posAccessor = gltf.accessors[p.findAttribute("POSITION")->accessorIndex];
			vertices.resize(vertices.size() + posAccessor.count);

			fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor,
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

				fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[(*normals).accessorIndex],
					[&](glm::vec3 v, size_t index) {
						vertices[initial_vtx + index].normal = v;
					});
			}

			// load UVs
			auto uv = p.findAttribute("TEXCOORD_0");
			if (uv != p.attributes.end()) {

				fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[(*uv).accessorIndex],
					[&](glm::vec2 v, size_t index) {
						vertices[initial_vtx + index].uv_x = v.x;
						vertices[initial_vtx + index].uv_y = v.y;
					});
			}

			// load vertex colors
			auto colors = p.findAttribute("COLOR_0");
			if (colors != p.attributes.end()) {

				fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[(*colors).accessorIndex],
					[&](glm::vec4 v, size_t index) {
						vertices[initial_vtx + index].color = v;
					});
			}

			if (p.materialIndex.has_value()) {
				newSurface.material = materials[p.materialIndex.value()];
			}
			else {
				newSurface.material = materials[0];
			}

			// Frustum Cullin bounds positions
			//loop the vertices of this surface, find min/max bounds
			glm::vec3 minpos = vertices[initial_vtx].position;
			glm::vec3 maxpos = vertices[initial_vtx].position;
			for (int i = static_cast<int>(initial_vtx); i < vertices.size(); i++) {
				minpos = glm::min(minpos, vertices[i].position);
				maxpos = glm::max(maxpos, vertices[i].position);
			}
			// calculate origin and extents from the min/max, use extent length for radius
			newSurface.bounds.origin = (maxpos + minpos) / 2.f;
			newSurface.bounds.extents = (maxpos - minpos) / 2.f;
			newSurface.bounds.sphereRadius = glm::length(newSurface.bounds.extents);

			newmesh->surfaces.push_back(newSurface);
		}

		newmesh->meshBuffers = uploadMesh(indices, vertices);
	}


	// load all nodes and their meshes
	for (fastgltf::Node& node : gltf.nodes) {
		std::shared_ptr<Node> newNode;

		// find if the node has a mesh, and if it does hook it to the mesh pointer and allocate it with the MeshNode struct
		if (node.meshIndex.has_value()) {
			newNode = std::make_shared<MeshNode>();
			static_cast<MeshNode*>(newNode.get())->mesh = meshes[*node.meshIndex];
		}
		else {
			newNode = std::make_shared<Node>();
		}

		nodes.push_back(newNode);
		file.nodes[node.name.c_str()];

		// load the GLTF transform data, and convert it into a gltf final transform matrix
		std::visit(fastgltf::visitor{
			[&](const fastgltf::math::fmat4x4& matrix) {
				memcpy(&newNode->localTransform, matrix.data(), sizeof(matrix));
			},
			[&](const fastgltf::TRS& transform) {
				glm::vec3 tl(transform.translation[0], transform.translation[1], transform.translation[2]);
				glm::quat rot(transform.rotation[3], transform.rotation[0], transform.rotation[1], transform.rotation[2]);
				glm::vec3 sc(transform.scale[0], transform.scale[1], transform.scale[2]);

				glm::mat4 tm = glm::translate(glm::mat4(1.f), tl);
				glm::mat4 rm = glm::toMat4(rot);
				glm::mat4 sm = glm::scale(glm::mat4(1.f), sc);

				newNode->localTransform = tm * rm * sm;
				}
		}, node.transform);
	}


	// run loop again to setup transform hierarchy
	for (int i = 0; i < gltf.nodes.size(); i++) {
		fastgltf::Node& node = gltf.nodes[i];
		std::shared_ptr<Node>& sceneNode = nodes[i];

		for (auto& c : node.children) {
			sceneNode->children.push_back(nodes[c]);
			nodes[c]->parent = sceneNode;
		}
	}

	// find the top nodes, with no parents
	for (auto& node : nodes) {
		if (node->parent.lock() == nullptr) {
			file.topNodes.push_back(node);
			node->refreshTransform(glm::mat4{ 1.f });
		}
	}

	return scene;
}

void LoadedGLTF::Draw(const glm::mat4& topMatrix, DrawContext& ctx) {
	// create renderables from the scenenodes
	for (auto& n : topNodes) {
		n->Draw(topMatrix, ctx);
	}
}

void LoadedGLTF::clearAll() {
	auto device = Backend::getDevice();
	auto allocator = AssetManager::getAssetAllocation();

	vkQueueWaitIdle(Backend::getGraphicsQueue());

	DescriptorSetOverwatch::assetDescriptorManager.destroyPools();

	BufferUtils::destroyBuffer(materialDataBuffer, allocator);

	for (auto& [k, v] : meshes) {
		BufferUtils::destroyBuffer(v->meshBuffers.indexBuffer, allocator);
		BufferUtils::destroyBuffer(v->meshBuffers.vertexBuffer, allocator);
	}

	for (auto& [k, v] : images) {
		if (v.image == AssetManager::getCheckboardTex().image) {
			//dont destroy the default images
			continue;
		}

		RendererUtils::destroyTexImage(device, v, allocator);
	}

	for (auto& sampler : samplers) {
		vkDestroySampler(device, sampler, nullptr);
	}
}
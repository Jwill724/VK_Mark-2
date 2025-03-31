#include "pch.h"

#include "AssetManager.h"

#include "Engine.h"
#include "vulkan/Backend.h"

#include "utils/BufferUtils.h"
#include "utils/VulkanUtils.h"
#include "utils/RendererUtils.h"

#include "renderer/CommandBuffer.h"

namespace AssetManager {
	std::vector<std::shared_ptr<MeshAsset>> testMeshes;
	std::vector<std::shared_ptr<MeshAsset>>& getTestMeshes() { return testMeshes; }

	DeletionQueue _assetDeletionQueue;
	DeletionQueue& getAssetDeletionQueue() { return _assetDeletionQueue; }
	VmaAllocator _assetAllocator;
	VmaAllocator& getAssetAllocation() { return _assetAllocator; }

	ImmCmdSubmitDef _immCmdSubmit{};
}

void AssetManager::transformMesh(GPUDrawPushConstants& pushConstants, float aspect) {
	glm::mat4 view = glm::translate(glm::vec3{ 0, 0, -5 });
	// camera projection
	glm::mat4 projection = glm::perspective(glm::radians(70.f), aspect, 1.f, 10.f);

	// invert the Y direction on projection matrix so that we are more similar
	// to opengl and gltf axis
	projection[1][1] *= -1;

	pushConstants.worldMatrix = projection * view;
}

void AssetManager::loadAssets() {
	_assetAllocator = VulkanUtils::createAllocator(Backend::getPhysicalDevice(), Backend::getDevice(), Backend::getInstance());
	_assetDeletionQueue.push_function([=] {
		vmaDestroyAllocator(_assetAllocator);
	});

	CommandBuffer::setupImmediateCmdBuffer(_immCmdSubmit);

	testMeshes = loadGltfMeshes("res/models/basicmesh.glb").value();
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
		constexpr bool OverrideColors = true;
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
	newSurface.vertexBuffer = BufferUtils::createBuffer(vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY, _assetAllocator);

	//find the address of the vertex buffer
	VkBufferDeviceAddressInfo deviceAdressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer = newSurface.vertexBuffer.buffer };
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
	_immCmdSubmit,
	Backend::getGraphicsQueue()
	);

	BufferUtils::destroyBuffer(staging, _assetAllocator);

	return newSurface;
}
#include "pch.h"

#include "AssetManager.h"
#include "Engine.h"
#include "vulkan/Backend.h"
#include "utils/BufferUtils.h"
#include "renderer/gpu_types/CommandBuffer.h"
#include "renderer/gpu_types/Descriptor.h"
#include "ResourceManager.h"
#include "renderer/RenderScene.h"
#include "utils/VulkanUtils.h"

namespace AssetManager {
	bool isValidMaterial(const fastgltf::Material& mat, const fastgltf::Asset& gltf);
	std::optional<std::shared_ptr<GLTFJobContext>> loadGltfFiles(std::string_view filePath);
}

bool AssetManager::loadGltf(ThreadContext& threadCtx) {
	ASSERT(threadCtx.workQueueActive != nullptr);

	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	ASSERT(queue && "[loadGltf] queue broken.");

	//std::string damagedHelmetPath { "res/assets/DamagedHelmet.glb" };
	//auto damagedHelmetFile = loadGltfFiles(damagedHelmetPath);
	//ASSERT(damagedHelmetFile.has_value());
	//damagedHelmetFile.value()->scene->sceneName = SceneNames.at(SceneID::DamagedHelmet);
	//queue->push(damagedHelmetFile.value());

	//std::string dragonPath { "res/assets/DragonAttenuation.glb" };
	//auto dragonFile = loadGltfFiles(dragonPath);
	//ASSERT(dragonFile.has_value());
	//dragonFile.value()->scene->sceneName = SceneNames.at(SceneID::DragonAttenuation);
	//queue->push(dragonFile.value());

	std::string sponza1Path { "res/assets/sponza.glb" };
	auto sponza1File = loadGltfFiles(sponza1Path);
	ASSERT(sponza1File.has_value());
	sponza1File.value()->scene->sceneName = SceneNames.at(SceneID::Sponza);
	queue->push(sponza1File.value());

	//std::string cubePath { "res/assets/basic_cube/Cube.gltf" };
	//auto cubeFile = loadGltfFiles(cubePath);
	//ASSERT(cubeFile.has_value());
	//cubeFile.value()->scene->sceneName = SceneNames.at(SceneID::Cube);
	//queue->push(cubeFile.value());

	//std::string spheresPath { "res/assets/MetalRoughSpheres.glb" };
	//auto spheresFile = loadGltfFiles(spheresPath);
	//ASSERT(spheresFile.has_value());
	//spheresFile.value()->scene->sceneName = SceneNames.at(SceneID::MRSpheres);
	//queue->push(spheresFile.value());

	if (!queue->empty()) {
		return true;
	}
	else {
		return false;
	}
}

std::optional<std::shared_ptr<GLTFJobContext>> AssetManager::loadGltfFiles(std::string_view filePath) {
	fmt::print("Loading GLTF: {}\n", filePath);

	auto context = std::make_shared<GLTFJobContext>();
	context->scene = std::make_shared<ModelAsset>();
	auto& scene = *context->scene;

	std::filesystem::path path = filePath;
	Engine::getState().getBasePath() = path.parent_path();
	scene.basePath = Engine::getState().getBasePath();
	fastgltf::Parser parser;

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

	switch (type) {
	case fastgltf::GltfType::glTF: {
		auto result = parser.loadGltf(data.get(), path.parent_path(), gltfOptions);
		if (!result || result.error() != fastgltf::Error::None) {
			fmt::print("Failed to parse .gltf: error code {}\n", static_cast<int>(result.error()));
			return std::nullopt;
		}
		context->gltfAsset = std::move(result.get());
		break;
	}
	case fastgltf::GltfType::GLB: {
		auto result = parser.loadGltfBinary(data.get(), path.parent_path(), gltfOptions);
		if (!result || result.error() != fastgltf::Error::None) {
			fmt::print("Failed to parse .glb: error code {}\n", static_cast<int>(result.error()));
			return std::nullopt;
		}
		context->gltfAsset = std::move(result.get());
		break;
	}
	default:
		fmt::print("Unknown or unsupported glTF file type\n");
		return std::nullopt;
	}

	return context;
}

// TODO: Multithread the shit out of this
// Largest bottle neck in the asset loading pipeline
void AssetManager::decodeImages(ThreadContext& threadCtx, const VmaAllocator allocator, DeletionQueue& bufferQueue) {
	ASSERT(threadCtx.workQueueActive != nullptr);

	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	ASSERT(queue && "[decodeImages] queue broken.");

	auto gltfJobs = queue->collect();
	for (auto& context : gltfJobs) {
		auto& gltf = context->gltfAsset;
		auto& scene = *context->scene;

		for (fastgltf::Image& image : gltf.images) {
			std::string name;
			if (!image.name.empty()) {
				name = image.name;
			}
			else if (std::holds_alternative<fastgltf::sources::URI>(image.data)) {
				name = std::string(std::get<fastgltf::sources::URI>(image.data).uri.c_str());
			}

			bool isSRGB =
				name.find("_BaseColor") != std::string::npos ||
				name.find("_Albedo") != std::string::npos ||
				name.find("diffuse") != std::string::npos;

			VkFormat format = isSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

			std::optional<AllocatedImage> img = Textures::loadImage(gltf, image, format, threadCtx, scene.basePath, allocator, bufferQueue);

			if (img.has_value()) {
				scene.runtime.images.push_back(*img);
			}
			else {
				// magenta and black for missing textures
				scene.runtime.images.push_back(ResourceManager::getCheckboardTex());
				fmt::print("gltf failed to load texture {}\n", image.name);
			}
		}

		queue->push(context);
		context->markJobComplete(GLTFJobType::DecodeImages);
	}
}

void AssetManager::buildSamplers(ThreadContext& threadCtx) {
	ASSERT(threadCtx.workQueueActive != nullptr);

	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	ASSERT(queue && "[buildSamplers] queue broken.");

	auto device = Backend::getDevice();

	auto gltfJobs = queue->collect();
	for (auto& context : gltfJobs) {
		auto& gltf = context->gltfAsset;
		auto& scene = *context->scene;

		for (fastgltf::Sampler& sampler : gltf.samplers) {
			VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .pNext = nullptr };
			sampl.maxLod = VK_LOD_CLAMP_NONE;
			sampl.minLod = 0.0f;

			sampl.magFilter = Textures::extract_filter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
			sampl.minFilter = Textures::extract_filter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

			sampl.mipmapMode = Textures::extract_mipmap_mode(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

			sampl.anisotropyEnable = VK_TRUE;
			sampl.maxAnisotropy = Backend::getDeviceLimits().maxSamplerAnisotropy;
			sampl.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			sampl.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			sampl.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

			VkSampler newSampler;
			VK_CHECK(vkCreateSampler(device, &sampl, nullptr, &newSampler));

			scene.runtime.samplers.push_back(newSampler);
		}

		queue->push(context);
		context->markJobComplete(GLTFJobType::BuildSamplers);
	}
}

void AssetManager::processMaterials(ThreadContext& threadCtx, const VmaAllocator allocator) {
	ASSERT(threadCtx.workQueueActive != nullptr);

	auto& imageTable = ResourceManager::_globalImageTable;
	auto& resources = Engine::getState().getGPUResources();

	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	ASSERT(queue && "[processMaterials] queue broken.");

	auto gltfJobs = queue->collect();

	// First count total materials
	size_t totalMaterialCount = 0;
	for (const auto& context : gltfJobs) {
		totalMaterialCount += context->gltfAsset.materials.size();
	}

	// Pre-allocate space for flat material staging
	std::vector<GPUMaterial> materialUploadList;
	materialUploadList.reserve(totalMaterialCount);

	for (auto& context : gltfJobs) {
		if (!context->isJobComplete(GLTFJobType::DecodeImages) ||
			!context->isJobComplete(GLTFJobType::BuildSamplers)) {
			continue;
		}

		auto& gltf = context->gltfAsset;
		auto& scene = *context->scene;
		scene.runtime.materials.clear();
		scene.runtime.materials.reserve(gltf.materials.size());

		for (fastgltf::Material& mat : gltf.materials) {
			if (!isValidMaterial(mat, gltf)) {
				fmt::print("Warning: Skipping invalid material: {}\n", mat.name);
				continue;
			}

			// Default/fallback images
			MaterialResources materialResources {
				.albedoImage = ResourceManager::getWhiteImage(),
				.albedoSampler = ResourceManager::getDefaultSamplerLinear(),
				.metalRoughImage = ResourceManager::getMetalRoughImage(),
				.metalRoughSampler = ResourceManager::getDefaultSamplerNearest(),
				.aoImage = ResourceManager::getAOImage(),
				.aoSampler = ResourceManager::getDefaultSamplerNearest(),
				.normalImage = ResourceManager::getNormalImage(),
				.normalSampler = ResourceManager::getDefaultSamplerLinear(),
				.emissiveImage = ResourceManager::getEmissiveImage(),
				.emissiveSampler = ResourceManager::getDefaultSamplerLinear(),
			};

			auto getImageAndSampler = [&](const fastgltf::TextureInfo& texInfo, AllocatedImage& outImg, VkSampler& outSamp) {
				const auto& texture = gltf.textures[texInfo.textureIndex];
				if (texture.imageIndex.has_value())
					outImg = scene.runtime.images[texture.imageIndex.value()];
				if (texture.samplerIndex.has_value())
					outSamp = scene.runtime.samplers[texture.samplerIndex.value()];
				};


			GPUMaterial newMaterial{};

			if (mat.pbrData.baseColorTexture.has_value()) {
				getImageAndSampler(*mat.pbrData.baseColorTexture, materialResources.albedoImage, materialResources.albedoSampler);
				newMaterial.colorFactor = glm::make_vec4(mat.pbrData.baseColorFactor.data());
			}

			if (mat.pbrData.metallicRoughnessTexture.has_value()) {
				getImageAndSampler(*mat.pbrData.metallicRoughnessTexture, materialResources.metalRoughImage, materialResources.metalRoughSampler);
				newMaterial.metalRoughFactors = glm::vec2(mat.pbrData.metallicFactor, mat.pbrData.roughnessFactor);
			}

			if (mat.normalTexture.has_value()) {
				getImageAndSampler(*mat.normalTexture, materialResources.normalImage, materialResources.normalSampler);
				newMaterial.normalScale = mat.normalTexture->scale;
			}

			if (mat.occlusionTexture.has_value()) {
				getImageAndSampler(*mat.occlusionTexture, materialResources.aoImage, materialResources.aoSampler);
				newMaterial.ambientOcclusion = mat.occlusionTexture->strength;
			}

			if (mat.emissiveTexture.has_value()) {
				getImageAndSampler(*mat.emissiveTexture, materialResources.emissiveImage, materialResources.emissiveSampler);
				newMaterial.emissiveStrength = mat.emissiveStrength;
			}

			if (mat.alphaMode == fastgltf::AlphaMode::Mask) {
				newMaterial.alphaCutoff = (mat.alphaCutoff != 0.0f) ? mat.alphaCutoff : 0.5f;
			}

			MaterialPass passType = MaterialPass::Opaque;
			if (mat.alphaMode == fastgltf::AlphaMode::Blend) {
				fmt::print("Material name: {}\n", mat.name);
				passType = MaterialPass::Transparent;
			}
			newMaterial.passType = static_cast<uint32_t>(passType);

			// LUT Indexing
			uint32_t albedoID = imageTable.pushCombined(materialResources.albedoImage.imageView, materialResources.albedoSampler);
			uint32_t metalRoughID = imageTable.pushCombined(materialResources.metalRoughImage.imageView, materialResources.metalRoughSampler);
			uint32_t normalID = imageTable.pushCombined(materialResources.normalImage.imageView, materialResources.normalSampler);
			uint32_t aoID = imageTable.pushCombined(materialResources.aoImage.imageView, materialResources.aoSampler);

			newMaterial.albedoID = albedoID;
			newMaterial.metalRoughnessID = metalRoughID;
			newMaterial.normalID = normalID;
			newMaterial.aoID = aoID;

			resources.addImageLUTEntry(ImageLUTEntry::CombinedOnly(albedoID));
			resources.addImageLUTEntry(ImageLUTEntry::CombinedOnly(metalRoughID));
			resources.addImageLUTEntry(ImageLUTEntry::CombinedOnly(normalID));
			resources.addImageLUTEntry(ImageLUTEntry::CombinedOnly(aoID));

			// Store in scene-local and global staging
			scene.runtime.materials.push_back(newMaterial);
			materialUploadList.push_back(newMaterial);
		}

		fmt::print("Scene Materials Processed: {}.\n", scene.runtime.materials.size());

		queue->push(context);
		context->markJobComplete(GLTFJobType::ProcessMaterials);
	}

	// Upload flattened materials
	const size_t totalMatBufSize = materialUploadList.size() * sizeof(GPUMaterial);
	AllocatedBuffer materialStaging = BufferUtils::createBuffer(
		totalMatBufSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
		allocator
	);

	memcpy(materialStaging.info.pMappedData, materialUploadList.data(), totalMatBufSize);

	AllocatedBuffer materialBuffer = BufferUtils::createGPUAddressBuffer(
		AddressBufferType::Material,
		resources.getAddressTable(),
		totalMatBufSize,
		allocator
	);
	resources.addGPUBufferToGlobalAddress(AddressBufferType::Material, materialBuffer);

	CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {
		VkBufferCopy copyRegion{};
		copyRegion.size = totalMatBufSize;
		vkCmdCopyBuffer(cmd, materialStaging.buffer, materialBuffer.buffer, 1, &copyRegion);
	}, threadCtx.cmdPool, QueueType::Transfer);

	resources.updateAddressTableMapped(threadCtx.cmdPool);

	auto matBuf = materialStaging.buffer;
	auto matAlloc = materialStaging.allocation;
	resources.getTempDeletionQueue().push_function([matBuf, matAlloc, allocator]() mutable {
		BufferUtils::destroyBuffer(matBuf, matAlloc, allocator);
	});
}

// Define Instances for models, meshIDs and materialIDs are setup here.
// A global meshes registry holds the mesh vector that'll be uploaded.
// DrawRangeID is the key to index and vertex buffers, meshbuffer owns this ID.
void AssetManager::processMeshes(
	ThreadContext& threadCtx,
	std::vector<GPUDrawRange>& drawRanges,
	MeshRegistry& meshes,
	std::vector<Vertex>& vertices,
	std::vector<uint32_t>& indices)
{
	ASSERT(threadCtx.workQueueActive != nullptr);

	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	ASSERT(queue && "[processMeshes] queue broken.");

	auto gltfJobs = queue->collect();

	for (auto& context : gltfJobs) {
		if (!context->isJobComplete(GLTFJobType::ProcessMaterials)) continue;

		auto& gltf = context->gltfAsset;
		auto& scene = *context->scene;

		scene.runtime.bakedInstances.clear();

		// Iterate over nodes that reference a mesh
		for (uint32_t nodeIdx = 0; nodeIdx < gltf.nodes.size(); ++nodeIdx) {
			const auto& node = gltf.nodes[nodeIdx];
			if (!node.meshIndex.has_value()) continue;

			uint32_t meshIdx = static_cast<uint32_t>(*node.meshIndex);
			const auto& mesh = gltf.meshes[meshIdx];

			for (uint32_t primIdx = 0; primIdx < mesh.primitives.size(); ++primIdx) {
				const auto& p = mesh.primitives[primIdx];

				auto inst = std::make_shared<BakedInstance>();
				inst->nodeID = nodeIdx;
				inst->gltfMeshIndex = meshIdx;
				inst->gltfPrimitiveIndex = primIdx; // Does nothing currently, one day...

				const uint32_t globalVertexOffset = static_cast<uint32_t>(vertices.size());
				const uint32_t globalIndexOffset = static_cast<uint32_t>(indices.size());

				const auto& posAccessor = gltf.accessors[p.findAttribute("POSITION")->accessorIndex];
				uint32_t vertexCount = static_cast<uint32_t>(posAccessor.count);
				vertices.resize(static_cast<size_t>(globalVertexOffset + vertexCount));

				fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor,
					[&](glm::vec3 v, size_t index) {
						ASSERT(globalVertexOffset + index < vertices.size());
						Vertex newvtx{};
						newvtx.position = v;
						newvtx.normal = glm::vec3(1.0f, 0.0f, 0.0f);
						newvtx.color = glm::vec4(1.0f);
						newvtx.uv = glm::vec2(0.0f);
						vertices[globalVertexOffset + index] = newvtx;
					});

				auto normals = p.findAttribute("NORMAL");
				if (normals != p.attributes.end()) {
					fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[normals->accessorIndex],
						[&](glm::vec3 v, size_t index) {
							vertices[globalVertexOffset + index].normal = v;
						});
				}

				auto uv = p.findAttribute("TEXCOORD_0");
				if (uv != p.attributes.end()) {
					fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[uv->accessorIndex],
						[&](glm::vec2 v, size_t index) {
							vertices[globalVertexOffset + index].uv = v;
						});
				}

				auto colors = p.findAttribute("COLOR_0");
				if (colors != p.attributes.end()) {
					fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[colors->accessorIndex],
						[&](glm::vec4 v, size_t index) {
							vertices[globalVertexOffset + index].color = v;
						});
				}

				const auto& indexAccessor = gltf.accessors[p.indicesAccessor.value()];
				uint32_t indexCount = static_cast<uint32_t>(indexAccessor.count);

				uint32_t maxIndex = 0;
				indices.reserve(static_cast<size_t>(globalIndexOffset + indexCount));

				fastgltf::iterateAccessorWithIndex<uint32_t>(gltf, indexAccessor,
					[&](uint32_t idx, size_t /*i*/) {
						maxIndex = std::max(maxIndex, idx);
						indices.push_back(idx);
					});

				ASSERT(globalVertexOffset + maxIndex < vertices.size() &&
					"Index buffer is referencing a vertex out of bounds!");

				GPUDrawRange range {
					.firstIndex = globalIndexOffset,
					.indexCount = indexCount,
					.vertexOffset = globalVertexOffset,
					.vertexCount = vertexCount
				};

				ASSERT(vertices.size() >= range.vertexOffset + range.vertexCount &&
					"Vertex buffer too small for range!");

				ASSERT(indices.size() >= range.firstIndex + range.indexCount &&
					"Index buffer too small for range!");

				GPUMeshData newMesh{};
				newMesh.drawRangeID = static_cast<uint32_t>(drawRanges.size());

				drawRanges.push_back(range);

				if (p.materialIndex.has_value()) {
					auto matID = p.materialIndex.value();
					inst->instance.materialID = static_cast<uint32_t>(matID);
					inst->passType = static_cast<MaterialPass>(scene.runtime.materials[static_cast<uint32_t>(matID)].passType);
				}
				else {
					inst->instance.materialID = 0;
					inst->passType = MaterialPass::Opaque;
				}

				glm::vec3 vmin = vertices[globalVertexOffset].position;
				glm::vec3 vmax = vmin;
				for (uint32_t i = 0; i < vertexCount; ++i) {
					glm::vec3 pos = vertices[static_cast<size_t>(globalVertexOffset + i)].position;
					vmin = glm::min(vmin, pos);
					vmax = glm::max(vmax, pos);
				}

				newMesh.localAABB.vmin = vmin;
				newMesh.localAABB.vmax = vmax;
				newMesh.localAABB.origin = (vmin + vmax) * 0.5f;
				newMesh.localAABB.extent = (vmax - vmin) * 0.5f;
				newMesh.localAABB.sphereRadius = glm::length(newMesh.localAABB.extent);

				inst->instance.meshID = meshes.registerMesh(newMesh);
				scene.runtime.bakedInstances.push_back(inst);
			}
		}

		fmt::print("[processMeshes] totals: meshes={}, verts={}, inds={}, ranges={}\n",
			meshes.meshData.size(),
			vertices.size(),
			indices.size(),
			drawRanges.size());

		ASSERT(!drawRanges.empty() && !vertices.empty() && !indices.empty() &&
			"Invalid draw range or empty mesh data");

		queue->push(context);
		context->markJobComplete(GLTFJobType::ProcessMeshes);
	}
}


bool AssetManager::isValidMaterial(const fastgltf::Material& mat, const fastgltf::Asset& gltf) {
	if (mat.pbrData.baseColorTexture.has_value()) {
		size_t texIndex = mat.pbrData.baseColorTexture.value().textureIndex;
		if (texIndex >= gltf.textures.size())
			return false;
		if (!gltf.textures[texIndex].imageIndex.has_value() ||
			gltf.textures[texIndex].imageIndex.value() >= gltf.images.size())
			return false;
		if (!gltf.textures[texIndex].samplerIndex.has_value() ||
			gltf.textures[texIndex].samplerIndex.value() >= gltf.samplers.size())
			return false;
	}
	return true;
}



void ModelAsset::FindVisibleInstances(
	std::vector<GPUInstance>& outVisibleOpaqueInstances,
	std::vector<GPUInstance>& outVisibleTransparentInstances,
	std::vector<glm::mat4>& outFrameTransformsList,
	const std::unordered_set<uint32_t> visibleMeshIDSet)
{
	for (const auto& root : scene.topNodes) {
		if (root) {
			root->FindVisibleInstances(
				outVisibleOpaqueInstances,
				outVisibleTransparentInstances,
				outFrameTransformsList,
				visibleMeshIDSet);
		}
	}
}

void ModelAsset::clearAll() {
	auto device = Backend::getDevice();
	const auto allocator = Engine::getState().getGPUResources().getAllocator();

	Backend::getGraphicsQueue().waitIdle();

	// Don't free global images or samplers twice
	for (auto& img : runtime.images) {
		if (img.image == VK_NULL_HANDLE ||
			img.image == ResourceManager::getCheckboardTex().image ||
			img.image == ResourceManager::getWhiteImage().image ||
			img.image == ResourceManager::getMetalRoughImage().image ||
			img.image == ResourceManager::getAOImage().image ||
			img.image == ResourceManager::getNormalImage().image ||
			img.image == ResourceManager::getEmissiveImage().image) {
			continue;
		}

		RendererUtils::destroyImage(device, img, allocator);
	}

	for (auto& sampler : runtime.samplers) {
		if (sampler == VK_NULL_HANDLE ||
			sampler == ResourceManager::getDefaultSamplerLinear() ||
			sampler == ResourceManager::getDefaultSamplerNearest()) {
			continue;
		}

		vkDestroySampler(device, sampler, nullptr);
	}
}
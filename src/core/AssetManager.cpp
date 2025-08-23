#include "pch.h"

#include "AssetManager.h"
#include "engine/Engine.h"
#include "renderer/Renderer.h"
#include "utils/VulkanUtils.h"
#include "utils/BufferUtils.h"

namespace AssetManager {
	bool isValidMaterial(const fastgltf::Material& mat, const fastgltf::Asset& gltf);
	std::optional<std::shared_ptr<GLTFJobContext>> loadGltfFiles(std::string_view filePath);
}

// TODO: dynamic loading and non hard coded models

bool AssetManager::loadGltf(ThreadContext& threadCtx) {
	ASSERT(threadCtx.workQueueActive != nullptr);

	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	ASSERT(queue && "[loadGltf] queue broken.");

	using namespace SceneGraph;

	std::string damagedHelmetPath{ "res/assets/DamagedHelmet.glb" };
	auto damagedHelmetFile = loadGltfFiles(damagedHelmetPath);
	ASSERT(damagedHelmetFile.has_value());
	damagedHelmetFile.value()->scene->sceneName = SceneNames.at(SceneID::DamagedHelmet);
	queue->push(damagedHelmetFile.value());

	//std::string sponza1Path{ "res/assets/sponza.glb" };
	//auto sponza1File = loadGltfFiles(sponza1Path);
	//ASSERT(sponza1File.has_value());
	//sponza1File.value()->scene->sceneName = SceneNames.at(SceneID::Sponza);
	//queue->push(sponza1File.value());

	//std::string dragonPath{ "res/assets/DragonAttenuation.glb" };
	//auto dragonFile = loadGltfFiles(dragonPath);
	//ASSERT(dragonFile.has_value());
	//dragonFile.value()->scene->sceneName = SceneNames.at(SceneID::DragonAttenuation);
	//queue->push(dragonFile.value());

	//std::string cubePath{ "res/assets/basic_cube/Cube.gltf" };
	//auto cubeFile = loadGltfFiles(cubePath);
	//ASSERT(cubeFile.has_value());
	//cubeFile.value()->scene->sceneName = SceneNames.at(SceneID::Cube);
	//queue->push(cubeFile.value());

	//std::string spheresPath{ "res/assets/MetalRoughSpheres.glb" };
	//auto spheresFile = loadGltfFiles(spheresPath);
	//ASSERT(spheresFile.has_value());
	//spheresFile.value()->scene->sceneName = SceneNames.at(SceneID::MRSpheres);
	//queue->push(spheresFile.value());

	// FIXME: Structure.glb is busted, transparency doesnt work and cpu bottle neck due to draw building

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
void AssetManager::decodeImages(
	ThreadContext& threadCtx,
	const VmaAllocator allocator,
	DeletionQueue& bufferQueue,
	const VkDevice device) {
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

			std::optional<AllocatedImage> img = TextureLoader::loadImage(
				gltf, image, format, threadCtx, scene.basePath, allocator, bufferQueue, device);

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

			sampl.magFilter = TextureLoader::extract_filter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
			sampl.minFilter = TextureLoader::extract_filter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

			sampl.mipmapMode = TextureLoader::extract_mipmap_mode(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

			sampl.anisotropyEnable = VK_TRUE;
			sampl.maxAnisotropy = Backend::getDeviceLimits().maxSamplerAnisotropy;

			VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			sampl.addressModeU = addressMode;
			sampl.addressModeV = addressMode;
			sampl.addressModeW = addressMode;

			VkSampler newSampler;
			VK_CHECK(vkCreateSampler(device, &sampl, nullptr, &newSampler));

			scene.runtime.samplers.push_back(newSampler);
		}

		queue->push(context);
		context->markJobComplete(GLTFJobType::BuildSamplers);
	}
}

void AssetManager::processMaterials(ThreadContext& threadCtx, const VmaAllocator allocator, const VkDevice device) {
	ASSERT(threadCtx.workQueueActive != nullptr);

	auto& imageManager = ResourceManager::_globalImageManager;
	auto& resources = Engine::getState().getGPUResources();

	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	ASSERT(queue && "[processMaterials] queue broken.");

	auto gltfJobs = queue->collect();

	// First count total materials
	size_t totalMaterialCount = 0;
	for (const auto& context : gltfJobs) {
		totalMaterialCount += context->gltfAsset.materials.size();
	}

	resources.stats.totalMaterialCount = static_cast<uint32_t>(totalMaterialCount);

	// Pre-allocate space for flat material staging
	std::vector<GPUMaterial> materialUploadList;
	materialUploadList.reserve(totalMaterialCount);

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

	// Default lut indexes
	const uint32_t defaultAlbedoID = imageManager.addCombinedImage(
		materialResources.albedoImage.imageView,
		materialResources.albedoSampler
	);
	const uint32_t defaultMetalRoughID = imageManager.addCombinedImage(
		materialResources.metalRoughImage.imageView,
		materialResources.metalRoughSampler
	);
	const uint32_t defaultNormalID = imageManager.addCombinedImage(
		materialResources.normalImage.imageView,
		materialResources.normalSampler
	);
	const uint32_t defaultAoID = imageManager.addCombinedImage(
		materialResources.aoImage.imageView,
		materialResources.aoSampler
	);
	const uint32_t defaultEmissiveID = imageManager.addCombinedImage(
		materialResources.emissiveImage.imageView,
		materialResources.emissiveSampler
	);

	resources.addImageLUTEntry(ImageLUTEntry::CombinedOnly(defaultAlbedoID));
	resources.addImageLUTEntry(ImageLUTEntry::CombinedOnly(defaultMetalRoughID));
	resources.addImageLUTEntry(ImageLUTEntry::CombinedOnly(defaultNormalID));
	resources.addImageLUTEntry(ImageLUTEntry::CombinedOnly(defaultAoID));
	resources.addImageLUTEntry(ImageLUTEntry::CombinedOnly(defaultEmissiveID));


	for (auto& context : gltfJobs) {
		if (!context->isJobComplete(GLTFJobType::DecodeImages) ||
			!context->isJobComplete(GLTFJobType::BuildSamplers)) {
			continue;
		}

		auto& gltf = context->gltfAsset;
		auto& scene = *context->scene;
		scene.runtime.materials.clear();
		scene.runtime.materials.reserve(gltf.materials.size());

		uint32_t currentMat = 0;
		for (fastgltf::Material& mat : gltf.materials) {
			if (!isValidMaterial(mat, gltf)) {
				fmt::print("Warning: Skipping invalid material\n");
				continue;
			}

			auto getImageAndSampler = [&](const fastgltf::TextureInfo& texInfo, AllocatedImage& outImg, VkSampler& outSamp) {
				const auto& texture = gltf.textures[texInfo.textureIndex];
				if (texture.imageIndex.has_value())
					outImg = scene.runtime.images[texture.imageIndex.value()];
				if (texture.samplerIndex.has_value())
					outSamp = scene.runtime.samplers[texture.samplerIndex.value()];
				};


			GPUMaterial newMaterial{};

			// Albedo
			if (mat.pbrData.baseColorTexture.has_value()) {
				getImageAndSampler(*mat.pbrData.baseColorTexture, materialResources.albedoImage, materialResources.albedoSampler);
				newMaterial.colorFactor = glm::make_vec4(mat.pbrData.baseColorFactor.data());
				newMaterial.albedoID = imageManager.addCombinedImage(
					materialResources.albedoImage.imageView,
					materialResources.albedoSampler
				);
			}
			else {
				newMaterial.albedoID = defaultAlbedoID;
			}

			// Metal roughness
			if (mat.pbrData.metallicRoughnessTexture.has_value()) {
				getImageAndSampler(*mat.pbrData.metallicRoughnessTexture, materialResources.metalRoughImage, materialResources.metalRoughSampler);
				newMaterial.metalRoughFactors = glm::vec2(mat.pbrData.metallicFactor, mat.pbrData.roughnessFactor);
				newMaterial.metalRoughnessID = imageManager.addCombinedImage(
					materialResources.metalRoughImage.imageView,
					materialResources.metalRoughSampler
				);
			}
			else {
				newMaterial.metalRoughnessID = defaultMetalRoughID;
			}

			// Normals
			if (mat.normalTexture.has_value()) {
				getImageAndSampler(*mat.normalTexture, materialResources.normalImage, materialResources.normalSampler);
				newMaterial.normalScale = mat.normalTexture->scale;
				newMaterial.normalID = imageManager.addCombinedImage(
					materialResources.normalImage.imageView,
					materialResources.normalSampler
				);
			}
			else {
				newMaterial.normalID = defaultNormalID;
			}

			// Ambient occlusion
			if (mat.occlusionTexture.has_value()) {
				getImageAndSampler(*mat.occlusionTexture, materialResources.aoImage, materialResources.aoSampler);
				newMaterial.ambientOcclusion = mat.occlusionTexture->strength;
				newMaterial.aoID = imageManager.addCombinedImage(
					materialResources.aoImage.imageView,
					materialResources.aoSampler
				);
			}
			else {
				newMaterial.aoID = defaultAoID;
			}

			// Emissive
			if (mat.emissiveTexture.has_value()) {
				getImageAndSampler(*mat.emissiveTexture, materialResources.emissiveImage, materialResources.emissiveSampler);
				newMaterial.emissiveColor = glm::make_vec3(mat.emissiveFactor.data());
				newMaterial.emissiveStrength = mat.emissiveStrength;
				newMaterial.emissiveID = imageManager.addCombinedImage(
					materialResources.emissiveImage.imageView,
					materialResources.emissiveSampler
				);
			}
			else {
				newMaterial.emissiveID = defaultEmissiveID;
			}

			if (mat.alphaMode == fastgltf::AlphaMode::Mask) {
				newMaterial.alphaCutoff = (mat.alphaCutoff != 0.0f) ? mat.alphaCutoff : 0.5f;
			}

			MaterialPass passType = MaterialPass::Opaque;
			if (mat.alphaMode == fastgltf::AlphaMode::Blend) {
				passType = MaterialPass::Transparent;
			}
			newMaterial.passType = static_cast<uint32_t>(passType);

			resources.addImageLUTEntry(ImageLUTEntry::CombinedOnly(newMaterial.albedoID));
			resources.addImageLUTEntry(ImageLUTEntry::CombinedOnly(newMaterial.metalRoughnessID));
			resources.addImageLUTEntry(ImageLUTEntry::CombinedOnly(newMaterial.normalID));
			resources.addImageLUTEntry(ImageLUTEntry::CombinedOnly(newMaterial.aoID));
			resources.addImageLUTEntry(ImageLUTEntry::CombinedOnly(newMaterial.emissiveID));

			fmt::print("[Material:{}] A:{} MR:{} N:{} AO:{} E:{}\n",
				currentMat,
				newMaterial.albedoID,
				newMaterial.metalRoughnessID,
				newMaterial.normalID,
				newMaterial.aoID,
				newMaterial.emissiveID);

			// Store in scene-local and global staging
			scene.runtime.materials.push_back(newMaterial);
			materialUploadList.push_back(newMaterial);

			currentMat++;
		}

		queue->push(context);
		context->markJobComplete(GLTFJobType::ProcessMaterials);
	}

	fmt::print("Scene Materials Processed: {}.\n", totalMaterialCount);

	// Upload flattened materials
	const size_t totalMatBufSize = totalMaterialCount * sizeof(GPUMaterial);
	AllocatedBuffer materialStaging = BufferUtils::createBuffer(
		totalMatBufSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
		allocator
	);

	memcpy(materialStaging.info.pMappedData, materialUploadList.data(), totalMatBufSize);

	// Material ssbo
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
	}, threadCtx.cmdPool, QueueType::Transfer, device);

	resources.updateAddressTableMapped(threadCtx.cmdPool);

	auto matBuf = materialStaging.buffer;
	auto matAlloc = materialStaging.allocation;
	resources.getTempDeletionQueue().push_function([matBuf, matAlloc, allocator]() mutable {
		BufferUtils::destroyBuffer(matBuf, matAlloc, allocator);
	});
}

// Define Instances for models, meshID, materialID are setup here.
// A global meshes registry holds the mesh vector that'll be uploaded.
// meshbuffer holds each localaabb and the range data into vertex and index buffers,
void AssetManager::processMeshes(
	ThreadContext& threadCtx,
	MeshRegistry& meshes,
	std::vector<Vertex>& vertices,
	std::vector<uint32_t>& indices)
{
	ASSERT(threadCtx.workQueueActive != nullptr);

	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	ASSERT(queue && "[processMeshes] queue broken.");

	auto gltfJobs = queue->collect();

	auto& resourceStats = Engine::getState().getGPUResources().stats;

	uint32_t matOffset = 0;

	for (auto& context : gltfJobs) {
		if (!context->isJobComplete(GLTFJobType::ProcessMaterials)) continue;

		auto& gltf = context->gltfAsset;
		auto& scene = *context->scene;

		scene.runtime.bakedInstances.clear();
		scene.runtime.bakedNodeIDs.clear();
		uint32_t sceneMatCount = static_cast<uint32_t>(scene.runtime.materials.size());

		// Iterate over nodes that reference a mesh
		for (uint32_t nodeIdx = 0; nodeIdx < gltf.nodes.size(); ++nodeIdx) {
			const auto& node = gltf.nodes[nodeIdx];

			if (!node.meshIndex.has_value()) continue;

			uint32_t meshIdx = static_cast<uint32_t>(*node.meshIndex);
			const auto& mesh = gltf.meshes[meshIdx];

			for (uint32_t primIdx = 0; primIdx < mesh.primitives.size(); ++primIdx) {
				const auto& p = mesh.primitives[primIdx];

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

				GPUMeshData newMesh {
					.firstIndex = globalIndexOffset,
					.indexCount = indexCount,
					.vertexOffset = globalVertexOffset,
					.vertexCount = vertexCount
				};

				ASSERT(vertices.size() >= newMesh.vertexOffset + newMesh.vertexCount &&
					"Vertex buffer too small for range!");

				ASSERT(indices.size() >= newMesh.firstIndex + newMesh.indexCount &&
					"Index buffer too small for range!");

				// Define baked instance in model
				auto inst = std::make_shared<GPUInstance>();

				if (p.materialIndex.has_value()) {
					auto matID = p.materialIndex.value();
					inst->materialID = static_cast<uint32_t>(matID) + matOffset;
					inst->passType = scene.runtime.materials[static_cast<uint32_t>(matID)].passType;
				}
				else {
					inst->materialID = matOffset;
					inst->passType = static_cast<uint32_t>(MaterialPass::Opaque);
				}
				ASSERT(inst->materialID < resourceStats.totalMaterialCount && "MaterialID out of range");

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

				inst->meshID = meshes.registerMesh(newMesh);
				scene.runtime.bakedInstances.push_back(inst);
				scene.runtime.bakedNodeIDs.push_back(nodeIdx);
			}
		}

		matOffset += sceneMatCount;

		resourceStats.totalMeshCount = static_cast<uint32_t>(meshes.meshData.size());
		resourceStats.totalVertexCount = static_cast<uint32_t>(vertices.size());
		resourceStats.totalIndexCount = static_cast<uint32_t>(indices.size());

		fmt::print("[processMeshes] totals: meshes={}, verts={}, inds={}\n",
			resourceStats.totalMeshCount,
			resourceStats.totalVertexCount,
			resourceStats.totalIndexCount);

		ASSERT(resourceStats.totalMeshCount > 0 && resourceStats.totalVertexCount > 0 && resourceStats.totalIndexCount > 0 &&
			"Invalid draw ranges.");

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

		ImageUtils::destroyImage(device, img, allocator);
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
#include "pch.h"

#include "AssetManager.h"
#include "engine/Engine.h"
#include "renderer/Renderer.h"
#include "utils/VulkanUtils.h"
#include "utils/BufferUtils.h"

// TODO: dynamic loading and non hard coded models

bool AssetManager::loadGltf(ThreadContext& threadCtx) {
	ASSERT(threadCtx.workQueueActive != nullptr);

	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	ASSERT(queue && "[loadGltf] queue broken.");

	//std::string spheresPath{ "res/assets/MetalRoughSpheres.glb" };
	//auto spheresFile = loadGltfFiles(spheresPath);
	//ASSERT(spheresFile.has_value());
	//spheresFile.value()->scene->sceneName = SceneNames.at(SceneID::MRSpheres);
	//queue->push(spheresFile.value());

	//// TODO: Use a script to download assets
	//// Currently this isn't apart of the repo as its 190mb, download through dropbox on repo page.
	//std::string bistroPath{ "res/assets/Bistro.glb" };
	//auto bistroFile = loadGltfFiles(bistroPath);
	//ASSERT(bistroFile.has_value());
	//bistroFile.value()->scene->sceneName = SceneNames.at(SceneID::Bistro);
	//queue->push(bistroFile.value());

	std::string sponza1Path{ "res/assets/sponza.glb" };
	auto sponza1File = loadGltfFiles(sponza1Path);
	ASSERT(sponza1File.has_value());
	sponza1File.value()->scene->sceneName = SceneNames.at(SceneID::Sponza);
	queue->push(sponza1File.value());

	//std::string duckPath{ "res/assets/Duck.glb" };
	//auto duckFile = loadGltfFiles(duckPath);
	//ASSERT(duckFile.has_value());
	//duckFile.value()->scene->sceneName = SceneNames.at(SceneID::Duck);
	//queue->push(duckFile.value());

	//std::string damagedHelmetPath{ "res/assets/DamagedHelmet.glb" };
	//auto damagedHelmetFile = loadGltfFiles(damagedHelmetPath);
	//ASSERT(damagedHelmetFile.has_value());
	//damagedHelmetFile.value()->scene->sceneName = SceneNames.at(SceneID::DamagedHelmet);
	//queue->push(damagedHelmetFile.value());

	//std::string dragonPath{ "res/assets/DragonAttenuation.glb" };
	//auto dragonFile = loadGltfFiles(dragonPath);
	//ASSERT(dragonFile.has_value());
	//dragonFile.value()->scene->sceneName = SceneNames.at(SceneID::DragonAttenuation);
	//queue->push(dragonFile.value());

	//std::string emissPath{ "res/assets/EmissiveStrengthTest.glb" };
	//auto emissFile = loadGltfFiles(emissPath);
	//ASSERT(emissFile.has_value());
	//emissFile.value()->scene->sceneName = SceneNames.at(SceneID::EmissiveTest);
	//queue->push(emissFile.value());

	//std::string wrathDragonPath{ "res/assets/wrath_of_the_dragon.glb" };
	//auto wrathDragonFile = loadGltfFiles(wrathDragonPath);
	//ASSERT(wrathDragonFile.has_value());
	//wrathDragonFile.value()->scene->sceneName = SceneNames.at(SceneID::WrathDragon);
	//queue->push(wrathDragonFile.value());

	//std::string cityPath{ "res/assets/city/town4new.glb" };
	//auto cityFile = loadGltfFiles(cityPath);
	//ASSERT(cityFile.has_value());
	//cityFile.value()->scene->sceneName = SceneNames.at(SceneID::City);
	//queue->push(cityFile.value());

	//std::string structurePath{ "res/assets/structure.glb" };
	//auto structureFile = loadGltfFiles(structurePath);
	//ASSERT(structureFile.has_value());
	//structureFile.value()->scene->sceneName = SceneNames.at(SceneID::Structure);
	//queue->push(structureFile.value());

	if (!queue->empty()) {
		return true;
	}
	else {
		return false;
	}
}

std::optional<std::shared_ptr<GLTFJobContext>> AssetManager::loadGltfFiles(std::string_view filePath) {
	fmt::println("Loading GLTF: {}", filePath);

	auto context = std::make_shared<GLTFJobContext>();
	context->scene = std::make_shared<ModelAsset>();
	auto& scene = *context->scene;

	std::filesystem::path path = filePath;
	Engine::getState().getBasePath() = path.parent_path();
	scene.basePath = Engine::getState().getBasePath();
	fastgltf::Parser parser;

	auto data = fastgltf::GltfDataBuffer::FromPath(path);
	if (!data || data.error() != fastgltf::Error::None) {
		fmt::println("Failed to load file: error code {}", static_cast<int>(data.error()));
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
			fmt::println("Failed to parse .gltf: error code {}", static_cast<int>(result.error()));
			return std::nullopt;
		}
		context->gltfAsset = std::move(result.get());
		break;
	}
	case fastgltf::GltfType::GLB: {
		auto result = parser.loadGltfBinary(data.get(), path.parent_path(), gltfOptions);
		if (!result || result.error() != fastgltf::Error::None) {
			fmt::println("Failed to parse .glb: error code {}", static_cast<int>(result.error()));
			return std::nullopt;
		}
		context->gltfAsset = std::move(result.get());
		break;
	}
	default:
		fmt::println("Unknown or unsupported glTF file type");
		return std::nullopt;
	}

	return context;
}

void AssetManager::decodeImages(
	ThreadContext& threadCtx,
	const VmaAllocator allocator,
	DeletionQueue& bufferQueue,
	const VkDevice device)
{
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
				JobSystem::log(threadCtx.threadID, fmt::format("gltf failed to load texture {}\n", image.name));
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

	const auto device = Backend::getDevice();

	auto gltfJobs = queue->collect();
	for (auto& context : gltfJobs) {
		auto& gltf = context->gltfAsset;
		auto& scene = *context->scene;

		for (fastgltf::Sampler& sampler : gltf.samplers) {

			VkFilter filter = TextureLoader::extract_filter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
			VkSamplerMipmapMode mipmapMode = TextureLoader::extract_mipmap_mode(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

			VkSampler newSampler = ImageUtils::createSampler(
				device,
				filter,
				VK_SAMPLER_ADDRESS_MODE_REPEAT,
				VK_LOD_CLAMP_NONE,
				CURRENT_AF_LVL,
				nullptr,
				mipmapMode);

			scene.runtime.samplers.push_back(newSampler);
		}

		queue->push(context);
		context->markJobComplete(GLTFJobType::BuildSamplers);
	}
}

void AssetManager::processMaterials(
	ThreadContext& threadCtx,
	const VmaAllocator allocator,
	const VkDevice device)
{
	ASSERT(threadCtx.workQueueActive != nullptr);

	auto& imageManager = ResourceManager::_globalImageManager;
	auto& resources = Engine::getState().getGPUResources();
	auto& modelStats = resources.modelDataCounts;

	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	ASSERT(queue && "[processMaterials] queue broken.");

	auto gltfJobs = queue->collect();

	// Collect global and local material counts
	for (const auto& context : gltfJobs) {
		auto& gltf = context->gltfAsset;
		auto& scene = *context->scene;
		scene.runtime.materialBaseOffset = modelStats.totalMaterialCount;
		scene.runtime.localMaterialCount = static_cast<uint32_t>(gltf.materials.size());
		scene.runtime.materials.clear();
		scene.runtime.materials.reserve(scene.runtime.localMaterialCount);
		modelStats.totalMaterialCount += scene.runtime.localMaterialCount;
	}

	// Pre-allocate space for flat material staging
	const uint32_t totalMatCount = modelStats.totalMaterialCount;
	std::vector<GPUMaterial> materialUploadList;
	materialUploadList.reserve(totalMatCount);

	const auto defaultLinear = ResourceManager::getDefaultSamplerLinear();
	const auto defaultNearest = ResourceManager::getDefaultSamplerNearest();

	// Default/fallback images
	MaterialResources materialResources {
		.albedoImage = ResourceManager::getWhiteMat(),
		.albedoSampler = defaultLinear,
		.metalRoughImage = ResourceManager::getMetalRoughMat(),
		.metalRoughSampler = defaultNearest,
		.aoImage = ResourceManager::getAOMat(),
		.aoSampler = defaultNearest,
		.normalImage = ResourceManager::getNormaMat(),
		.normalSampler = defaultLinear,
		.emissiveImage = ResourceManager::getEmissiveMat(),
		.emissiveSampler = defaultLinear,
	};

	// Default lut indexes
	const uint32_t defaultAlbedoID = imageManager.addCombinedImage(
		materialResources.albedoImage.imageView,
		materialResources.albedoSampler,
		&threadCtx
	);
	const uint32_t defaultMetalRoughID = imageManager.addCombinedImage(
		materialResources.metalRoughImage.imageView,
		materialResources.metalRoughSampler,
		&threadCtx
	);
	const uint32_t defaultNormalID = imageManager.addCombinedImage(
		materialResources.normalImage.imageView,
		materialResources.normalSampler,
		&threadCtx
	);
	const uint32_t defaultAoID = imageManager.addCombinedImage(
		materialResources.aoImage.imageView,
		materialResources.aoSampler,
		&threadCtx
	);
	const uint32_t defaultEmissiveID = imageManager.addCombinedImage(
		materialResources.emissiveImage.imageView,
		materialResources.emissiveSampler,
		&threadCtx
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

		uint32_t currentMat = 0;
		for (fastgltf::Material& mat : gltf.materials) {
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
					materialResources.albedoSampler,
					&threadCtx
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
					materialResources.metalRoughSampler,
					&threadCtx
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
					materialResources.normalSampler,
					&threadCtx
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
					materialResources.aoSampler,
					&threadCtx
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
					materialResources.emissiveSampler,
					&threadCtx
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

			if (ENABLE_DEBUG_LOGS) {
				JobSystem::log(threadCtx.threadID,
					fmt::format("[Material:{}] A:{} MR:{} N:{} AO:{} E:{}\n",
						currentMat,
						newMaterial.albedoID,
						newMaterial.metalRoughnessID,
						newMaterial.normalID,
						newMaterial.aoID,
						newMaterial.emissiveID));
			}

			// Store in scene-local and global staging
			scene.runtime.materials.push_back(newMaterial);
			materialUploadList.push_back(newMaterial);

			currentMat++;
		}

		queue->push(context);
		context->markJobComplete(GLTFJobType::ProcessMaterials);
	}

	ASSERT(!materialUploadList.empty() && "Material list is invalid.");

	// Upload flattened materials
	const size_t totalMatBufSize = totalMatCount * sizeof(GPUMaterial);
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

	auto matBuf = materialStaging.buffer;
	auto matAlloc = materialStaging.allocation;
	resources.getTempDQueue().push_function([matBuf, matAlloc, allocator]() mutable {
		BufferUtils::destroyBuffer(matBuf, matAlloc, allocator);
	});
}

// Define Instances for models, meshID, materialID are setup here.
// A global meshes registry holds the mesh vector that'll be uploaded.
// meshbuffer holds each localaabb and the range data into vertex and index buffers,
void AssetManager::processMeshes(
	ThreadContext & threadCtx,
	MeshRegistry & meshes,
	std::vector<Vertex>&vertices,
	std::vector<uint32_t>&indices,
	ModelDataCounts& modelDataCounts)
{
	ASSERT(threadCtx.workQueueActive != nullptr);

	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	ASSERT(queue && "[processMeshes] queue broken.");

	auto gltfJobs = queue->collect();

	// Compute total vertex/index counts per scene
	size_t totalVertexCount = 0;
	size_t totalIndexCount = 0;

	for (auto& context : gltfJobs) {
		auto& gltf = context->gltfAsset;
		auto& scene = *context->scene;

		// Grab offsets first
		scene.runtime.vertexOffset = totalVertexCount;
		scene.runtime.indexOffset = totalIndexCount;

		for (uint32_t nodeIdx = 0; nodeIdx < gltf.nodes.size(); ++nodeIdx) {
			const auto& node = gltf.nodes[nodeIdx];
			if (!node.meshIndex.has_value()) continue;

			const auto& mesh = gltf.meshes[*node.meshIndex];

			for (auto& p : mesh.primitives) {
				if (auto posAttr = p.findAttribute("POSITION"); posAttr != p.attributes.end()) {
					totalVertexCount += gltf.accessors[posAttr->accessorIndex].count;
				}
				if (p.indicesAccessor.has_value()) {
					totalIndexCount += gltf.accessors[p.indicesAccessor.value()].count;
				}
			}
		}

		// Define scene total counts
		scene.runtime.vertexCount = totalVertexCount - scene.runtime.vertexOffset;
		scene.runtime.indexCount = totalIndexCount - scene.runtime.indexOffset;
		ASSERT(scene.runtime.vertexCount > 0);
		ASSERT(scene.runtime.indexCount > 0);
	}

	// Reserve big enough buffers once
	modelDataCounts.totalVertexCount = static_cast<uint32_t>(totalVertexCount);
	modelDataCounts.totalIndexCount = static_cast<uint32_t>(totalIndexCount);
	vertices.resize(totalVertexCount);
	indices.resize(totalIndexCount);


	// Fill pass
	for (auto& context : gltfJobs) {
		if (!context->isJobComplete(GLTFJobType::ProcessMaterials)) continue;

		auto& gltf = context->gltfAsset;
		auto& scene = *context->scene;

		scene.runtime.bakedInstances.clear();
		scene.runtime.bakedNodeIDs.clear();

		// Base offsets for this scene
		const size_t sceneVertexBase = scene.runtime.vertexOffset;
		const size_t sceneIndexBase = scene.runtime.indexOffset;

		// Local cursors into this scene's global slice
		size_t sceneVertexCursor = 0;
		size_t sceneIndexCursor = 0;

		uint32_t localMeshCount = 0;

		for (uint32_t nodeIdx = 0; nodeIdx < gltf.nodes.size(); ++nodeIdx) {
			const auto& node = gltf.nodes[nodeIdx];
			if (!node.meshIndex.has_value()) continue;

			const auto& mesh = gltf.meshes[*node.meshIndex];

			for (auto& p : mesh.primitives) {
				// Vertex accessor
				const auto& posAccessor = gltf.accessors[p.findAttribute("POSITION")->accessorIndex];
				const uint32_t vertexCount = static_cast<uint32_t>(posAccessor.count);

				const size_t vtxOff = sceneVertexBase + sceneVertexCursor;

				fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor,
					[&](glm::vec3 v, size_t i) {
						ASSERT(vtxOff + i < vertices.size());
						Vertex vtx{};
						vtx.position = v;
						vertices[vtxOff + i] = vtx;
					});

				// Fill other attributes
				if (auto normals = p.findAttribute("NORMAL"); normals != p.attributes.end()) {
					fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[normals->accessorIndex],
						[&](glm::vec3 v, size_t i) {
							vertices[vtxOff + i].normal = v;
						});
				}
				if (auto uv = p.findAttribute("TEXCOORD_0"); uv != p.attributes.end()) {
					fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[uv->accessorIndex],
						[&](glm::vec2 v, size_t i) {
							vertices[vtxOff + i].uv = v;
						});
				}
				if (auto colors = p.findAttribute("COLOR_0"); colors != p.attributes.end()) {
					fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[colors->accessorIndex],
						[&](glm::vec4 v, size_t i) {
							vertices[vtxOff + i].color = v;
						});
				}

				// Indices
				const auto& indexAccessor = gltf.accessors[p.indicesAccessor.value()];
				const uint32_t indexCount = static_cast<uint32_t>(indexAccessor.count);

				const size_t idxOff = sceneIndexBase + sceneIndexCursor;

				uint32_t maxIndex = 0;
				fastgltf::iterateAccessorWithIndex<uint32_t>(gltf, indexAccessor,
					[&](uint32_t idx, size_t j) {
						indices[idxOff + j] = idx;
						maxIndex = std::max(maxIndex, idx);
					});

				ASSERT(vtxOff + maxIndex < vertices.size() &&
					"Index buffer is referencing a vertex out of bounds!");

				// Register mesh
				GPUMeshData newMesh {
					.firstIndex = static_cast<uint32_t>(idxOff),
					.indexCount = indexCount,
					.vertexOffset = static_cast<uint32_t>(vtxOff),
					.vertexCount = vertexCount
				};

				// AABB computation
				glm::vec3 vmin = vertices[vtxOff].position;
				glm::vec3 vmax = vmin;
				for (uint32_t i = 0; i < vertexCount; ++i) {
					glm::vec3 pos = vertices[vtxOff + static_cast<size_t>(i)].position;
					vmin = glm::min(vmin, pos);
					vmax = glm::max(vmax, pos);
				}

				newMesh.localAABB.vmin = vmin;
				newMesh.localAABB.vmax = vmax;
				newMesh.localAABB.origin = (vmin + vmax) * 0.5f;
				newMesh.localAABB.extent = (vmax - vmin) * 0.5f;
				newMesh.localAABB.sphereRadius = glm::length(newMesh.localAABB.extent);

				GPUInstance newInst{};

				// Attach local materialID and account for global bindless array,
				// as well as pass types.
				if (p.materialIndex.has_value()) {
					auto bindlessMatID = scene.runtime.materialBaseOffset + p.materialIndex.value();
					auto localMatID = p.materialIndex.value();
					newInst.materialID = static_cast<uint32_t>(bindlessMatID); // ID into the gpu bindless array
					newInst.passType = scene.runtime.materials[localMatID].passType;
				}
				else {
					// Default material types
					newInst.materialID = static_cast<uint32_t>(scene.runtime.materialBaseOffset);
					newInst.passType = static_cast<uint32_t>(MaterialPass::Opaque);
				}
				ASSERT(newInst.materialID < modelDataCounts.totalMaterialCount && "MaterialID out of range");

				newInst.meshID = meshes.registerMesh(newMesh);
				scene.runtime.bakedInstances.push_back(newInst);
				scene.runtime.bakedNodeIDs.push_back(nodeIdx);

				// Advance cursors
				sceneVertexCursor += vertexCount;
				sceneIndexCursor += indexCount;
				localMeshCount++;
			}
		}

		if (ENABLE_DEBUG_LOGS) {
			JobSystem::log(threadCtx.threadID,
				fmt::format("[processMeshes] totals: meshes={}, verts={}, inds={}\n",
					localMeshCount,
					scene.runtime.vertexCount,
					scene.runtime.indexCount));
		}

		queue->push(context);
		context->markJobComplete(GLTFJobType::ProcessMeshes);
	}

	modelDataCounts.totalMeshCount = static_cast<uint32_t>(meshes.meshData.size());

	ASSERT(modelDataCounts.totalMeshCount > 0 &&
		modelDataCounts.totalVertexCount > 0 &&
		modelDataCounts.totalIndexCount > 0 &&
		"Invalid draw ranges.");
}

void AssetManager::buildSceneGraph(
	ThreadContext& threadCtx,
	std::vector<GlobalInstance>& globalInstances,
	std::vector<glm::mat4>& globalTransforms,
	ModelDataCounts& modelDataCounts)
{
	ASSERT(threadCtx.workQueueActive != nullptr);
	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	ASSERT(queue);

	auto gltfJobs = queue->collect();

	uint32_t instanceCounter = 0;
	uint32_t firstTransform = 0;

	int gridCols = 2;       // how many models per row
	float spacingX = 100.0f; // horizontal spacing
	float spacingZ = 100.0f; // depth spacing

	// Increasing y of model spawns
	float yOffsetPerInstance = 2.5f;

	for (auto& context : gltfJobs) {
		if (!context->isComplete()) continue;

		auto& gltf = context->gltfAsset;
		auto& modelAsset = *context->scene;

		// === Build all nodes ===
		std::vector<std::shared_ptr<Node>> nodes;
		nodes.reserve(gltf.nodes.size());
		for (size_t i = 0; i < gltf.nodes.size(); ++i) {
			const auto& srcNode = gltf.nodes[i];
			auto node = std::make_shared<Node>();

			std::visit(fastgltf::visitor{
				[&](const fastgltf::math::fmat4x4& matrix) {
					node->localTransform = glm::make_mat4x4(matrix.data());
				},
				[&](const fastgltf::TRS& transform) {
					glm::vec3 tl(transform.translation[0], transform.translation[1], transform.translation[2]);
					glm::quat rot(transform.rotation[3], transform.rotation[0], transform.rotation[1], transform.rotation[2]);
					glm::vec3 sc(transform.scale[0], transform.scale[1], transform.scale[2]);
					node->localTransform =
						glm::translate(glm::mat4(1.0f), tl) *
						glm::toMat4(rot) *
						glm::scale(glm::mat4(1.0f), sc);
				}
			}, srcNode.transform);

			nodes.push_back(node);
		}

		// === Parent-child relationships ===
		for (size_t i = 0; i < gltf.nodes.size(); ++i) {
			for (auto childIdx : gltf.nodes[i].children) {
				nodes[i]->children.push_back(nodes[static_cast<size_t>(childIdx)]);
				nodes[static_cast<size_t>(childIdx)]->parent = nodes[i];
			}
		}

		// === Find root nodes ===
		modelAsset.sceneNodes.topNodes.clear();
		for (auto& node : nodes) {
			if (node->parent.expired()) {
				modelAsset.sceneNodes.topNodes.push_back(node);
			}
		}

		// === Compute world transforms ===
		for (auto& node : modelAsset.sceneNodes.topNodes) {
			node->refreshTransform(glm::mat4(1.0f));
		}
		modelAsset.sceneNodes.nodes = nodes;

		// define model with a sceneID
		SceneID sceneID = SceneIDs.at(modelAsset.sceneName);
		modelAsset.sceneID = sceneID;

		// === Assign global instances ===
		GlobalInstance gblInst{};
		gblInst.sceneID = static_cast<uint8_t>(sceneID);

		const auto& bakedInstances = modelAsset.runtime.bakedInstances;
		const auto& bakedNodeIDs = modelAsset.runtime.bakedNodeIDs;

		ASSERT(bakedNodeIDs.size() == bakedInstances.size() && "[BuildSceneGraph]: bakedNodes should equal bakedInstances.");

		// Build unique node set + local->slot map from bakedNodeIDs
		modelAsset.runtime.uniqueNodeIDs.clear();
		modelAsset.runtime.localToNodeSlot.resize(bakedNodeIDs.size());

		std::unordered_map<uint32_t, uint32_t> nodeToSlot;
		modelAsset.runtime.uniqueNodeIDs.reserve(bakedNodeIDs.size());

		for (size_t i = 0; i < bakedNodeIDs.size(); ++i) {
			const uint32_t nodeIdx = static_cast<uint32_t>(bakedNodeIDs[i]);
			auto it = nodeToSlot.find(nodeIdx);
			uint32_t slot = 0;
			if (it == nodeToSlot.end()) {
				slot = static_cast<uint32_t>(modelAsset.runtime.uniqueNodeIDs.size());
				nodeToSlot.emplace(nodeIdx, slot);
				modelAsset.runtime.uniqueNodeIDs.push_back(nodeIdx);
			}
			else {
				slot = it->second;
			}
			modelAsset.runtime.localToNodeSlot[i] = slot;
		}

		gblInst.perInstanceStride = static_cast<uint32_t>(bakedInstances.size());
		gblInst.transformCount = static_cast<uint32_t>(modelAsset.runtime.uniqueNodeIDs.size());

		// Placing assets on top of eachother
		//float currentY = instanceCounter * yOffsetPerInstance;
		//gblInst.modelOffset = glm::vec3(0.0f, currentY, 0.0f);

		// Spreads out assets in even planes as grids
		int row = instanceCounter / gridCols;
		int col = instanceCounter % gridCols;
		gblInst.modelOffset = glm::vec3(col * spacingX, 0.0f, row * spacingZ);

		// === Push unique transforms into the global list ===
		gblInst.firstTransform = firstTransform;
		for (uint32_t i = 0; i < gblInst.transformCount; ++i) {
			const uint32_t nodeIdx = modelAsset.runtime.uniqueNodeIDs[i];
			glm::mat4 world = nodes[nodeIdx]->worldTransform;

			// Shift model into its own grid cell
			world = glm::translate(glm::mat4(1.0f), gblInst.modelOffset) * world;
			globalTransforms.push_back(world);
		}
		firstTransform += gblInst.transformCount;
		modelDataCounts.totalTransformCount += gblInst.transformCount;

		gblInst.instanceID = instanceCounter++;
		globalInstances.push_back(gblInst);

		if (ENABLE_DEBUG_LOGS) {
			JobSystem::log(threadCtx.threadID,
				fmt::format("SceneGraph built: '{}'. Total bakedInstances = {}. Total materials = {}. Total transforms = {}\n",
					modelAsset.sceneName,
					bakedInstances.size(),
					modelAsset.runtime.materials.size(),
					gblInst.transformCount));
		}

		queue->push(context);
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
			img.image == ResourceManager::getWhiteMat().image ||
			img.image == ResourceManager::getMetalRoughMat().image ||
			img.image == ResourceManager::getAOMat().image ||
			img.image == ResourceManager::getNormaMat().image ||
			img.image == ResourceManager::getEmissiveMat().image) {
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
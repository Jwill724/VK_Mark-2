#include "pch.h"

#include "AssetManager.h"
#include "engine/Engine.h"
#include "renderer/Renderer.h"
#include "utils/VulkanUtils.h"
#include "utils/BufferUtils.h"
#include "common/Mesh.h"
#include "common/Vertex.h"

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

	// TODO: Use a script to download assets
	// Currently this isn't apart of the repo as its 190mb, download through dropbox on repo page.
	std::string bistroPath{ "res/assets/Bistro.glb" };
	auto bistroFile = loadGltfFiles(bistroPath);
	ASSERT(bistroFile.has_value());
	bistroFile.value()->scene->sceneName = SceneNames.at(SceneID::Bistro);
	queue->Push(bistroFile.value());

	//std::string sponza1Path{ "res/assets/sponza.glb" };
	//auto sponza1File = loadGltfFiles(sponza1Path);
	//ASSERT(sponza1File.has_value());
	//sponza1File.value()->scene->sceneName = SceneNames.at(SceneID::Sponza);
	//queue->push(sponza1File.value());

	//std::string mechPath{ "res/assets/mech.glb" };
	//auto mechFile = loadGltfFiles(mechPath);
	//ASSERT(mechFile.has_value());
	//mechFile.value()->scene->sceneName = SceneNames.at(SceneID::Mech);
	//queue->push(mechFile.value());

	//std::string yellowMechPath{ "res/assets/yellow_mech.glb" };
	//auto yellowMechFile = loadGltfFiles(yellowMechPath);
	//ASSERT(yellowMechFile.has_value());
	//yellowMechFile.value()->scene->sceneName = SceneNames.at(SceneID::YellowMech);
	//queue->push(yellowMechFile.value());

	//std::string miniPath{ "res/assets/mini.glb" };
	//auto miniFile = loadGltfFiles(miniPath);
	//ASSERT(miniFile.has_value());
	//miniFile.value()->scene->sceneName = SceneNames.at(SceneID::Mini);
	//queue->push(miniFile.value());

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

	//std::string structurePath{ "res/assets/structure.glb" };
	//auto structureFile = loadGltfFiles(structurePath);
	//ASSERT(structureFile.has_value());
	//structureFile.value()->scene->sceneName = SceneNames.at(SceneID::Structure);
	//queue->push(structureFile.value());

	//std::string cityPath{ "res/assets/city/town4new.glb" };
	//auto cityFile = loadGltfFiles(cityPath);
	//ASSERT(cityFile.has_value());
	//cityFile.value()->scene->sceneName = SceneNames.at(SceneID::City);
	//queue->push(cityFile.value());


	return !queue->Empty();
}

std::optional<std::shared_ptr<GLTFJobContext>> AssetManager::loadGltfFiles(std::string_view filePath) {
	fmt::println("Loading GLTF: {}", filePath);

	auto context = std::make_shared<GLTFJobContext>();
	context->scene = std::make_shared<ModelAsset>();
	auto& scene = *context->scene;

	std::filesystem::path path = filePath;
	Engine::GetState().getBasePath() = path.parent_path();
	scene.basePath = Engine::GetState().getBasePath();
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

	auto gltfJobs = queue->Collect();
	for (auto& context : gltfJobs) {
		auto& gltf = context->gltfAsset;
		auto& scene = *context->scene;

		scene.runtime.images.clear();
		scene.runtime.images.reserve(gltf.images.size());

		for (size_t imgIdx = 0; imgIdx < gltf.images.size(); imgIdx++) {
			fastgltf::Image& image = gltf.images[imgIdx];
			std::string imageName;
			if (!image.name.empty()) {
				imageName = image.name;
			}
			else if (std::holds_alternative<fastgltf::sources::URI>(image.data)) {
				imageName = std::string(std::get<fastgltf::sources::URI>(image.data).uri.c_str());
			}

			VkFormat imageFormat = getImageFormatFromName(imageName);

			std::optional<AllocatedImage> loadedImage = TextureLoader::loadImage(
				gltf,
				image,
				imageFormat,
				threadCtx,
				scene.basePath,
				allocator,
				bufferQueue,
				device
			);

			RuntimeImage runtimeImage{};

			if (loadedImage.has_value()) {
				runtimeImage.image = *loadedImage;
				runtimeImage.semantic = MaterialType::Unknown;
			}
			else {
				runtimeImage.image = ResourceManager::GetCheckboard_Texture();
				runtimeImage.semantic = MaterialType::Unknown;

				JobSystem::Log(
					threadCtx.threadID,
					fmt::format("gltf failed to load texture {}\n", image.name)
				);
			}

			scene.runtime.images.push_back(runtimeImage);
		}

		queue->Push(context);
		context->markJobComplete(GLTFJobType::DecodeImages);
	}
}


void AssetManager::buildSamplers(ThreadContext& threadCtx) {
	ASSERT(threadCtx.workQueueActive != nullptr);

	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	ASSERT(queue && "[buildSamplers] queue broken.");

	const auto device = Backend::GetDevice();

	auto gltfJobs = queue->Collect();
	for (auto& context : gltfJobs) {
		auto& gltf = context->gltfAsset;
		auto& scene = *context->scene;

		for (fastgltf::Sampler& sampler : gltf.samplers) {
			VkFilter magFilter = TextureLoader::extract_filter(
				sampler.magFilter.value_or(fastgltf::Filter::Linear));

			VkFilter minFilter = TextureLoader::extract_filter(
				sampler.minFilter.value_or(fastgltf::Filter::LinearMipMapLinear));

			VkSamplerMipmapMode mipmapMode = TextureLoader::extract_mipmap_mode(
				sampler.minFilter.value_or(fastgltf::Filter::LinearMipMapLinear));

			VkSampler newSampler = ImageUtils::createSampler(
				device,
				minFilter,
				VK_SAMPLER_ADDRESS_MODE_REPEAT,
				VK_LOD_CLAMP_NONE,
				CURRENT_AF_LVL,
				nullptr,
				mipmapMode);

			scene.runtime.samplers.push_back(newSampler);
		}

		queue->Push(context);
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
	auto& resources = Engine::GetState().getGPUResources();
	auto& modelStats = resources.modelDataCounts;

	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	ASSERT(queue && "[processMaterials] queue broken.");

	auto gltfJobs = queue->Collect();

	// Collect global and local material counts
	for (const auto& context : gltfJobs) {
		auto& gltf = context->gltfAsset;
		auto& scene = *context->scene;
		scene.runtime.materialBaseOffset = modelStats.totalMaterialCount;

		scene.runtime.localMaterialCount = static_cast<uint32_t>(gltf.materials.size());
		// For assets that don't contain materials
		if (scene.runtime.localMaterialCount == 0) scene.runtime.localMaterialCount = 1u;

		scene.runtime.materials.clear();
		scene.runtime.materials.reserve(scene.runtime.localMaterialCount);
		modelStats.totalMaterialCount += scene.runtime.localMaterialCount;
	}

	// Pre-allocate space for flat material staging
	const uint32_t totalMatCount = modelStats.totalMaterialCount;
	std::vector<Material> materialUploadList;
	materialUploadList.reserve(totalMatCount);

	const auto defaultLinear = ResourceManager::GetDefaultLinear_Sampler();
	const auto defaultNearest = ResourceManager::GetDefaultNearest_Sampler();

	// Default/fallback images
	MaterialResources materialResources {
		.albedoImage = ResourceManager::GetWhiteMat_Texture(),
		.albedoSampler = defaultLinear,
		.metalRoughImage = ResourceManager::GetMetalRough_Texture(),
		.metalRoughSampler = defaultNearest,
		.normalImage = ResourceManager::GetNormal_Texture(),
		.normalSampler = defaultLinear,
		.emissiveImage = ResourceManager::GetEmissive_Texture(),
		.emissiveSampler = defaultLinear,
	};

	// Default lut indexes
	const uint32_t defaultAlbedoID = imageManager.AddCombinedImage(
		materialResources.albedoImage.imageView,
		materialResources.albedoSampler,
		&threadCtx
	);
	const uint32_t defaultMetalRoughID = imageManager.AddCombinedImage(
		materialResources.metalRoughImage.imageView,
		materialResources.metalRoughSampler,
		&threadCtx
	);
	const uint32_t defaultNormalID = imageManager.AddCombinedImage(
		materialResources.normalImage.imageView,
		materialResources.normalSampler,
		&threadCtx
	);
	const uint32_t defaultEmissiveID = imageManager.AddCombinedImage(
		materialResources.emissiveImage.imageView,
		materialResources.emissiveSampler,
		&threadCtx
	);

	resources.AddImageLUTEntry(ImageLUTEntry::CombinedOnly(defaultAlbedoID));
	resources.AddImageLUTEntry(ImageLUTEntry::CombinedOnly(defaultMetalRoughID));
	resources.AddImageLUTEntry(ImageLUTEntry::CombinedOnly(defaultNormalID));
	resources.AddImageLUTEntry(ImageLUTEntry::CombinedOnly(defaultEmissiveID));

	auto& materialFlags = resources.GetMaterialFlagsByID();
	materialFlags.clear();

	materialFlags.resize(totalMatCount, 0u);

	for (auto& context : gltfJobs) {
		if (!context->isJobComplete(GLTFJobType::DecodeImages) ||
			!context->isJobComplete(GLTFJobType::BuildSamplers)) {
			continue;
		}

		auto& gltf = context->gltfAsset;
		auto& scene = *context->scene;

		const uint32_t baseOffset = static_cast<uint32_t>(scene.runtime.materialBaseOffset);

		uint32_t currentMat = 0;

		// Default material
		if (gltf.materials.empty()) {
			fmt::println("Asset includes no materials, assigning default.");

			Material newMaterial{};
			newMaterial.albedoID = defaultAlbedoID;
			newMaterial.metalRoughnessID = defaultMetalRoughID;
			newMaterial.normalID = defaultNormalID;
			newMaterial.emissiveID = defaultEmissiveID;
			newMaterial.passType = static_cast<uint32_t>(MaterialPass::OPAQUE);
			newMaterial.flags |= MATERIAL_FLAG_CASTS_SHADOWS;

			resources.AddImageLUTEntry(ImageLUTEntry::CombinedOnly(newMaterial.albedoID));
			resources.AddImageLUTEntry(ImageLUTEntry::CombinedOnly(newMaterial.metalRoughnessID));
			resources.AddImageLUTEntry(ImageLUTEntry::CombinedOnly(newMaterial.normalID));
			resources.AddImageLUTEntry(ImageLUTEntry::CombinedOnly(newMaterial.emissiveID));

			materialFlags[static_cast<size_t>(baseOffset + currentMat)] = newMaterial.flags;

			scene.runtime.materials.push_back(newMaterial);
			materialUploadList.push_back(newMaterial);

			currentMat++;
		}

		for (fastgltf::Material& mat : gltf.materials) {
			auto getImageAndSampler = [&](const fastgltf::TextureInfo& texInfo, AllocatedImage& outImg, VkSampler& outSamp) {
				const auto& texture = gltf.textures[texInfo.textureIndex];
				if (texture.imageIndex.has_value())
					outImg = scene.runtime.images[texture.imageIndex.value()].image;
				if (texture.samplerIndex.has_value())
					outSamp = scene.runtime.samplers[texture.samplerIndex.value()];
			};
			auto markTextureSemantic = [&](const fastgltf::TextureInfo& texInfo, MaterialType semantic) {
				const auto& texture = gltf.textures[texInfo.textureIndex];

				if (!texture.imageIndex.has_value()) return;

				const uint32_t imageIndex = static_cast<uint32_t>(texture.imageIndex.value());
				setRuntimeImageSemantic(scene.runtime, imageIndex, semantic);
			};



			Material newMaterial{};

			// Albedo
			if (mat.pbrData.baseColorTexture.has_value()) {
				markTextureSemantic(*mat.pbrData.baseColorTexture, MaterialType::Albedo);
				getImageAndSampler(*mat.pbrData.baseColorTexture, materialResources.albedoImage, materialResources.albedoSampler);
				newMaterial.colorFactor = glm::make_vec4(mat.pbrData.baseColorFactor.data());
				newMaterial.albedoID = imageManager.AddCombinedImage(
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
				markTextureSemantic(*mat.pbrData.metallicRoughnessTexture, MaterialType::MetalRoughness);
				getImageAndSampler(*mat.pbrData.metallicRoughnessTexture, materialResources.metalRoughImage, materialResources.metalRoughSampler);
				newMaterial.metalRoughFactors = glm::vec2(mat.pbrData.metallicFactor, mat.pbrData.roughnessFactor);
				newMaterial.metalRoughnessID = imageManager.AddCombinedImage(
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
				markTextureSemantic(*mat.normalTexture, MaterialType::Normal);
				getImageAndSampler(*mat.normalTexture, materialResources.normalImage, materialResources.normalSampler);
				newMaterial.normalScale = mat.normalTexture->scale;
				newMaterial.normalID = imageManager.AddCombinedImage(
					materialResources.normalImage.imageView,
					materialResources.normalSampler,
					&threadCtx
				);
				newMaterial.flags |= MATERIAL_FLAG_HAS_NORMAL_MAP;
			}
			else {
				newMaterial.normalID = defaultNormalID;
			}

			// Emissive
			if (mat.emissiveTexture.has_value()) {
				markTextureSemantic(*mat.emissiveTexture, MaterialType::Emissive);
				getImageAndSampler(*mat.emissiveTexture, materialResources.emissiveImage, materialResources.emissiveSampler);
				newMaterial.emissiveColor = glm::make_vec3(mat.emissiveFactor.data());
				newMaterial.emissiveStrength = mat.emissiveStrength;
				newMaterial.emissiveID = imageManager.AddCombinedImage(
					materialResources.emissiveImage.imageView,
					materialResources.emissiveSampler,
					&threadCtx
				);
			}
			else {
				newMaterial.emissiveID = defaultEmissiveID;
			}

			// Default mat: cast shadows and opaque
			MaterialPass passType = MaterialPass::OPAQUE;
			newMaterial.flags |= MATERIAL_FLAG_CASTS_SHADOWS;

			if (mat.alphaMode == fastgltf::AlphaMode::Mask) {
				newMaterial.alphaCutoff = (mat.alphaCutoff != 0.0f) ? mat.alphaCutoff : 0.5f;
				newMaterial.flags |= MATERIAL_FLAG_ALPHA_MASKED;
			}

			if (mat.alphaMode == fastgltf::AlphaMode::Blend) {
				passType = MaterialPass::TRANSPARENT;
				newMaterial.flags &= ~MATERIAL_FLAG_CASTS_SHADOWS;
			}
			newMaterial.passType = static_cast<uint32_t>(passType);

			if (isTreeMaterial(mat, gltf)) {
				newMaterial.flags |= MATERIAL_FLAG_IS_TREE;
			}

			materialFlags[static_cast<size_t>(baseOffset + currentMat)] = newMaterial.flags;

			resources.AddImageLUTEntry(ImageLUTEntry::CombinedOnly(newMaterial.albedoID));
			resources.AddImageLUTEntry(ImageLUTEntry::CombinedOnly(newMaterial.metalRoughnessID));
			resources.AddImageLUTEntry(ImageLUTEntry::CombinedOnly(newMaterial.normalID));
			resources.AddImageLUTEntry(ImageLUTEntry::CombinedOnly(newMaterial.emissiveID));

			if (ENABLE_DEBUG_LOGS) {
				JobSystem::Log(threadCtx.threadID,
					fmt::format("[Material:{}] A:{} MR:{} N:{} AO:{} E:{}\n",
						currentMat,
						newMaterial.albedoID,
						newMaterial.metalRoughnessID,
						newMaterial.normalID,
						newMaterial.emissiveID));
			}

			// Store in scene-local and global staging
			scene.runtime.materials.push_back(newMaterial);
			materialUploadList.push_back(newMaterial);

			currentMat++;
		}

		if (ENABLE_DEBUG_LOGS) {
			for (uint32_t imageIndex = 0; imageIndex < scene.runtime.images.size(); imageIndex++) {
				JobSystem::Log(
					threadCtx.threadID,
					fmt::format(
						"[Image:{}] semantic={}\n",
						imageIndex,
						textureSemanticToString(scene.runtime.images[imageIndex].semantic)
					)
				);
			}
		}

		queue->Push(context);
		context->markJobComplete(GLTFJobType::ProcessMaterials);
	}

	ASSERT(!materialUploadList.empty() && "Material list is invalid.");

	// Upload flattened materials
	const size_t totalMatBufSize = totalMatCount * sizeof(Material);
	AllocatedBuffer materialStaging = BufferUtils::CreateBuffer(
		totalMatBufSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
		allocator
	);

	memcpy(materialStaging.m_allocInfo.pMappedData, materialUploadList.data(), totalMatBufSize);

	// Material ssbo
	AllocatedBuffer materialBuffer = BufferUtils::CreateGPUAddressBuffer(
		BufferSlot::Material,
		resources.GetAddressTable(),
		totalMatBufSize,
		allocator
	);
	resources.AddGPUBufferToGlobalAddress(BufferSlot::Material, materialBuffer);

	CommandBuffer::RecordDeferredCmd([&](VkCommandBuffer cmd) {
		VkBufferCopy copyRegion{};
		copyRegion.size = totalMatBufSize;
		vkCmdCopyBuffer(cmd, materialStaging.m_buffer, materialBuffer.m_buffer, 1, &copyRegion);
	}, threadCtx.cmdPool, QueueType::Transfer, device);

	auto matBuf = materialStaging.m_buffer;
	auto matAlloc = materialStaging.m_allocation;
	resources.GetTempDQueue().PushFunction([matBuf, matAlloc, allocator]() mutable {
		BufferUtils::DestroyBuffer(matBuf, matAlloc, allocator);
	});
}


// Define Instances for models, meshID, materialID are setup here.
// A global meshes registry holds the mesh vector that'll be uploaded.
// meshbuffer holds each localaabb and the range data into vertex and index buffers,
void AssetManager::processMeshes(
	ThreadContext& threadCtx,
	MeshRegistry& meshes,
	std::vector<Vertex>& vertices,
	std::vector<uint32_t>& indices,
	TotalAssetDataCounts& modelDataCounts)
{
	ASSERT(threadCtx.workQueueActive != nullptr);

	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	ASSERT(queue && "[processMeshes] queue broken.");

	auto gltfJobs = queue->Collect();

	// Compute total vertex/index counts per scene
	size_t totalVertexCount = 0;
	size_t totalIndexCount = 0;

	for (auto& context : gltfJobs) {
		auto& gltf = context->gltfAsset;
		auto& scene = *context->scene;

		// Grab offsets first
		scene.runtime.vertexOffset = totalVertexCount;
		scene.runtime.indexOffset = totalIndexCount;

		for (uint32_t nodeIdx = 0; nodeIdx < gltf.nodes.size(); ++nodeIdx)
		{
			const auto& node = gltf.nodes[nodeIdx];
			if (!node.meshIndex.has_value()) continue;

			const auto& mesh = gltf.meshes[*node.meshIndex];

			for (auto& p : mesh.primitives)
			{
				if (auto posAttr = p.findAttribute("POSITION"); posAttr != p.attributes.end())
				{
					totalVertexCount += gltf.accessors[posAttr->accessorIndex].count;
				}
				if (p.indicesAccessor.has_value())
				{
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

	std::vector<Vertex> optimizedVertices;
	std::vector<uint32_t> lodIndex;
	std::vector<uint32_t> baseIndexCopy;
	std::vector<glm::vec3> tempPositions;
	tempPositions.resize(totalVertexCount);

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
						Vertex vertex{};
						EncodePosition(vertex, v);

						vertices[vtxOff + i] = vertex;
						tempPositions[i] = v;
					});

				if (auto normals = p.findAttribute("NORMAL"); normals != p.attributes.end()) {
					fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[normals->accessorIndex],
						[&](glm::vec3 v, size_t i) {
							Vertex& vertex = vertices[vtxOff + i];
							EncodeOctahedral_Normal(vertex, v);
						});
				}

				if (auto tangents = p.findAttribute("TANGENT"); tangents != p.attributes.end()) {
					fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[tangents->accessorIndex],
						[&](glm::vec4 v, size_t i) {
							Vertex& vertex = vertices[vtxOff + i];
							EncodeOctahedral_Tangent(vertex, v);
						});
				}

				if (auto uv = p.findAttribute("TEXCOORD_0"); uv != p.attributes.end()) {
					fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[uv->accessorIndex],
						[&](glm::vec2 v, size_t i) {
							Vertex& vertex = vertices[vtxOff + i];

							vertex.uvX = FloatToHalfBits(v.x);
							vertex.uvY = FloatToHalfBits(v.y);
						});
				}

				if (auto colors = p.findAttribute("COLOR_0"); colors != p.attributes.end()) {
					fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[colors->accessorIndex],
						[&](glm::vec4 v, size_t i) {
							Vertex& vertex = vertices[vtxOff + i];

							EncodeRGBA8(vertex, v);
						});
				}

				// Indices
				ASSERT(p.indicesAccessor.has_value() && "[processMeshes] primitive missing indices accessor.");

				const auto& indexAccessor = gltf.accessors[p.indicesAccessor.value()];
				const uint32_t indexCount = static_cast<uint32_t>(indexAccessor.count);

				const size_t idxOff = sceneIndexBase + sceneIndexCursor;

				uint32_t maxIndex = 0;
				fastgltf::iterateAccessorWithIndex<uint32_t>(gltf, indexAccessor,
					[&](uint32_t idx, size_t j) {
						ASSERT(idxOff + j < indices.size());
						indices[idxOff + j] = idx;
						maxIndex = std::max(maxIndex, idx);
					});

				ASSERT(maxIndex < vertexCount &&
					"Index buffer is referencing a vertex out of bounds for this primitive!");

				uint32_t* indexData = indices.data() + idxOff;
				Vertex* vertexData = vertices.data() + vtxOff;

				meshopt_optimizeVertexCache(
					indexData,
					indexData,
					static_cast<size_t>(indexCount),
					static_cast<size_t>(vertexCount));


				if ((indexCount % 3u) == 0u) {
					meshopt_optimizeOverdraw(
						indexData,
						indexData,
						static_cast<size_t>(indexCount),
						reinterpret_cast<const float*>(tempPositions.data()),
						static_cast<size_t>(vertexCount),
						sizeof(Vertex),
						1.05f);
				}

				optimizedVertices.resize(static_cast<size_t>(vertexCount));

				meshopt_optimizeVertexFetch(
					optimizedVertices.data(),
					indexData,
					static_cast<size_t>(indexCount),
					vertexData,
					static_cast<size_t>(vertexCount),
					sizeof(Vertex));

				std::memcpy(
					vertexData,
					optimizedVertices.data(),
					static_cast<size_t>(vertexCount) * sizeof(Vertex));

			#ifndef NDEBUG
				uint32_t optimizedMaxIndex = 0;
				for (uint32_t k = 0; k < indexCount; ++k) {
					optimizedMaxIndex = std::max(optimizedMaxIndex, indexData[k]);
				}
				ASSERT(optimizedMaxIndex < vertexCount && "[meshopt] optimized indices out of bounds.");
			#endif

				baseIndexCopy.resize(static_cast<size_t>(indexCount));
				std::memcpy(
					baseIndexCopy.data(),
					indexData,
					static_cast<size_t>(indexCount) * sizeof(uint32_t));


				// Register mesh
				Mesh newMesh {
					.firstIndex = static_cast<uint32_t>(idxOff),
					.indexCount = indexCount,
					.vertexOffset = static_cast<uint32_t>(vtxOff),
					.vertexCount = vertexCount
				};

				// AABB computation
				glm::vec3 vmin = tempPositions[vtxOff];
				glm::vec3 vmax = vmin;
				for (uint32_t i = 0; i < vertexCount; ++i) {
					glm::vec3 pos = tempPositions[vtxOff + static_cast<size_t>(i)];
					vmin = glm::min(vmin, pos);
					vmax = glm::max(vmax, pos);
				}

				newMesh.localAABB.vmin = vmin;
				newMesh.localAABB.vmax = vmax;
				glm::vec3 extent = (vmax - vmin) * 0.5f;

				Instance newInst{};

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
					newInst.passType = static_cast<uint32_t>(MaterialPass::OPAQUE);
				}
				ASSERT(newInst.materialID < modelDataCounts.totalMaterialCount && "MaterialID out of range");

				// Mesh ID registration
				newInst.meshID = meshes.RegisterMesh(newMesh);

				const float* positionPtr = reinterpret_cast<const float*>(tempPositions.data());

				// Mesh LOD setup
				if ((indexCount % 3u) == 0u && positionPtr != nullptr) {
					meshes.ResizeMeshLods();

					const float simplifyScale = meshopt_simplifyScale(
						positionPtr,
						static_cast<size_t>(vertexCount),
						sizeof(Vertex));

					const uint32_t lod0MeshID = newInst.meshID;

					const uint32_t lod1 =  meshes.BuildLOD(
						newMesh,
						0.60f,
						0.005f,
						totalVertexCount,
						totalIndexCount,
						indices,
						lodIndex,
						baseIndexCopy,
						simplifyScale,
						positionPtr);

					const uint32_t lod2 =  meshes.BuildLOD(
						newMesh,
						0.35f,
						0.01f,
						totalVertexCount,
						totalIndexCount,
						indices,
						lodIndex,
						baseIndexCopy,
						simplifyScale,
						positionPtr);

					const uint32_t lod3 =  meshes.BuildLOD(
						newMesh,
						0.20f,
						0.02f,
						totalVertexCount,
						totalIndexCount,
						indices,
						lodIndex,
						baseIndexCopy,
						simplifyScale,
						positionPtr);


					MeshLODs& lods = meshes.meshLODs[lod0MeshID];
					lods.lod0 = lod0MeshID;
					lods.lod1 = (lod1 != UINT32_MAX) ? lod1 : lod0MeshID;
					lods.lod2 = (lod2 != UINT32_MAX) ? lod2 : lods.lod1;
					lods.lod3 = (lod3 != UINT32_MAX) ? lod3 : lods.lod2;

					// Special shadow lods
					lods.shadowLod0 = lod0MeshID;
					const bool forceShadowLod0 =
						MeshRegistry::IsThinMeshForShadows(newMesh) ||
						(indexCount < 300u) ||
						(newInst.passType == static_cast<uint32_t>(MaterialPass::OPAQUE));

					// Meshes that could be hard to simplify or too small are just given highest lod
					if (forceShadowLod0) {
						lods.flags |= MESH_LOD_FLAG_FORCE_SHADOW_LOD0;
						lods.shadowLod1 = lod0MeshID;
						lods.shadowLod2 = lod0MeshID;
					}
					// A more conservative lod setup than regular meshes
					else {
						const uint32_t shadow1 = meshes.BuildLOD(
							newMesh,
							0.75f,
							0.002f,
							totalVertexCount,
							totalIndexCount,
							indices,
							lodIndex,
							baseIndexCopy,
							simplifyScale,
							positionPtr);

						const uint32_t shadow2 = meshes.BuildLOD(
							newMesh,
							0.55f,
							0.004f,
							totalVertexCount,
							totalIndexCount,
							indices,
							lodIndex,
							baseIndexCopy,
							simplifyScale,
							positionPtr);

						lods.shadowLod1 = (shadow1 != UINT32_MAX) ? shadow1 : lods.shadowLod0;
						lods.shadowLod2 = (shadow2 != UINT32_MAX) ? shadow2 : lods.shadowLod1;
					}
				}

				scene.runtime.bakedInstances.push_back(newInst);
				scene.runtime.bakedNodeIDs.push_back(nodeIdx);

				// Advance cursors
				sceneVertexCursor += vertexCount;
				sceneIndexCursor += indexCount;
				localMeshCount++;
			}
		}

		if (ENABLE_DEBUG_LOGS) {
			JobSystem::Log(threadCtx.threadID,
				fmt::format("[processMeshes] totals: meshes={}, verts={}, inds={}\n",
					localMeshCount,
					scene.runtime.vertexCount,
					scene.runtime.indexCount));
		}

		queue->Push(context);
		context->markJobComplete(GLTFJobType::ProcessMeshes);
	}

	// Shadow indices setup on final list of indices and vertices
	const size_t shadowIndexBase = indices.size();
	indices.resize(shadowIndexBase + shadowIndexBase);
	for (auto& mesh : meshes.meshData) {
		mesh.shadowFirstIndex = static_cast<uint32_t>(shadowIndexBase + mesh.firstIndex);
		mesh.shadowIndexCount = mesh.indexCount;

		uint32_t* destination = indices.data() + mesh.shadowFirstIndex;
		const uint32_t* source = indices.data() + mesh.firstIndex;

		const Vertex* vertexData = vertices.data() + mesh.vertexOffset;

		meshopt_Stream streams[1]{};
		streams[0].data =
			reinterpret_cast<const uint8_t*>(vertexData) + offsetof(Vertex, position);
		streams[0].size = sizeof(float) * 3u;
		streams[0].stride = sizeof(Vertex);

		meshopt_generateShadowIndexBufferMulti(
			destination,
			source,
			static_cast<size_t>(mesh.shadowIndexCount),
			static_cast<size_t>(mesh.vertexCount),
			streams,
			1u);
	}

	modelDataCounts.totalVertexCount = static_cast<uint32_t>(vertices.size());
	modelDataCounts.totalIndexCount = static_cast<uint32_t>(indices.size());
	modelDataCounts.totalMeshCount = meshes.GetMeshCount();

	ASSERT(modelDataCounts.totalMeshCount > 0 &&
		modelDataCounts.totalVertexCount > 0 &&
		modelDataCounts.totalIndexCount > 0 &&
		"Invalid draw ranges.");
}

void AssetManager::buildSceneGraph(
	ThreadContext& threadCtx,
	std::vector<VirtualInstance>& globalInstances,
	std::vector<glm::mat4>& globalTransforms,
	TotalAssetDataCounts& modelDataCounts)
{
	ASSERT(threadCtx.workQueueActive != nullptr);
	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	ASSERT(queue);

	auto gltfJobs = queue->Collect();

	uint32_t instanceCounter = 0;
	uint32_t firstTransform = 0;

	int gridCols = 4;       // how many models per row
	float spacingX = 15.0f; // horizontal spacing
	float spacingZ = 15.0f; // depth spacing

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
		VirtualInstance gblInst{};
		gblInst.sceneID = static_cast<uint8_t>(sceneID);

		const auto& bakedInstances = modelAsset.runtime.bakedInstances;
		const auto& bakedNodeIDs = modelAsset.runtime.bakedNodeIDs;

		ASSERT(bakedNodeIDs.size() == bakedInstances.size() && "[BuildSceneGraph]: bakedNodes should equal bakedInstances.");

		// Build unique node m_frameSet + local->slot map from bakedNodeIDs
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

		// Spreads out assets in even planes as grids
		int row = instanceCounter / gridCols;
		int col = instanceCounter % gridCols;
		gblInst.modelOffset = glm::vec3(col * spacingX, 0.0f, row * spacingZ);

		// === Push unique transforms into the global list ===
		gblInst.firstTransform = firstTransform;
		for (uint32_t i = 0; i < gblInst.transformCount; ++i) {
			const uint32_t nodeIdx = modelAsset.runtime.uniqueNodeIDs[i];
			glm::mat4 world = nodes[nodeIdx]->worldTransform;

			// First base transform in an asset
			if (i == 0) {
				gblInst.baseTransform = world;
			}

			// Shift model into its own grid cell
			world = glm::translate(glm::mat4(1.0f), gblInst.modelOffset) * world;
			globalTransforms.push_back(world);
		}
		firstTransform += gblInst.transformCount;
		modelDataCounts.totalTransformCount += gblInst.transformCount;

		gblInst.instanceID = instanceCounter++;
		globalInstances.push_back(gblInst);

		if (ENABLE_DEBUG_LOGS) {
			JobSystem::Log(threadCtx.threadID,
				fmt::format("SceneGraph built: '{}'. Total bakedInstances = {}. Total materials = {}. Total transforms = {}\n",
					modelAsset.sceneName,
					bakedInstances.size(),
					modelAsset.runtime.materials.size(),
					gblInst.transformCount));
		}

		queue->Push(context);
	}
}


void ModelAsset::clearAll() {
	auto device = Backend::GetDevice();
	const auto allocator = Engine::GetState().getGPUResources().GetAllocator();

	Backend::GetGraphicsQueue().WaitIdle();

	// Don't free global images or samplers twice
	for (auto& img : runtime.images) {
		// holy naming
		if (img.image.image == VK_NULL_HANDLE ||
			img.image.image == ResourceManager::GetCheckboard_Texture().image ||
			img.image.image == ResourceManager::GetWhiteMat_Texture().image ||
			img.image.image == ResourceManager::GetMetalRough_Texture().image ||
			img.image.image == ResourceManager::getAO_Texture().image ||
			img.image.image == ResourceManager::GetNormal_Texture().image ||
			img.image.image == ResourceManager::GetEmissive_Texture().image) {
			continue;
		}

		ImageUtils::destroyImage(device, img.image, allocator);
	}

	for (auto& sampler : runtime.samplers) {
		if (sampler == VK_NULL_HANDLE ||
			sampler == ResourceManager::GetDefaultLinear_Sampler() ||
			sampler == ResourceManager::GetDefaultNearest_Sampler()) {
			continue;
		}

		vkDestroySampler(device, sampler, nullptr);
	}
}

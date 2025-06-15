#include "pch.h"

#include "AssetManager.h"
#include "Engine.h"
#include "vulkan/Backend.h"
#include "utils/BufferUtils.h"
#include "renderer/gpu/CommandBuffer.h"
#include "renderer/gpu/Descriptor.h"
#include "ResourceManager.h"
#include "renderer/RenderScene.h"
#include "utils/VulkanUtils.h"

namespace AssetManager {
	bool isValidMaterial(const fastgltf::Material& mat, const fastgltf::Asset& gltf);
	std::optional<std::shared_ptr<GLTFJobContext>> loadGltfFiles(std::string_view filePath);
}


bool AssetManager::loadGltf(ThreadContext& threadCtx) {
	assert(threadCtx.workQueueActive != nullptr);

	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	assert(queue && "[loadGltf] queue broken.");

	//std::string damagedHelmetPath = { "res/assets/DamagedHelmet.glb" };
	//auto damagedHelmetFile = loadGltfFiles(damagedHelmetPath);
	//assert(damagedHelmetFile.has_value());
	//damagedHelmetFile.value()->scene->sceneName = SceneNames.at(SceneID::DamagedHelmet);
	//queue->push(damagedHelmetFile.value());

	//std::string sponza1Path = { "res/assets/sponza.glb" };
	//auto sponza1File = loadGltfFiles(sponza1Path);
	//assert(sponza1File.has_value());
	//sponza1File.value()->scene->sceneName = SceneNames.at(SceneID::Sponza);
	//queue->push(sponza1File.value());

	//std::string cubePath = { "res/assets/basic_cube/Cube.gltf" };
	//auto cubeFile = loadGltfFiles(cubePath);
	//assert(cubeFile.has_value());
	//cubeFile.value()->scene->sceneName = SceneNames.at(SceneID::Cube);
	//queue->push(cubeFile.value());

	//std::string spheresPath = { "res/assets/MetalRoughSpheres.glb" };
	//auto spheresFile = loadGltfFiles(spheresPath);
	//assert(spheresFile.has_value());
	//spheresFile.value()->scene->sceneName = SceneNames.at(SceneID::MRSpheres);
	//queue->push(spheresFile.value());
//
//	//std::string sponza2Path = { "res/assets/Sponza/base/NewSponza_Main_glTF_003.gltf" };
//	//auto sponza2File = loadGltf(sponza2Path, device);
//	//assert(sponza2File.has_value());
//	//RenderScene::_loadedScenes["sponza2"] = *sponza2File;
//
//	//std::string sponza2CurtainsPath = { "res/assets/Sponza/curtains/NewSponza_Curtains_glTF.gltf" };
//	//auto sponza2CurtainsFile = loadGltf(sponza2CurtainsPath, device);
//	//assert(sponza2CurtainsFile.has_value());
//	//RenderScene::_loadedScenes["sponza2Curtains"] = *sponza2CurtainsFile;

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
	context->scene = std::make_shared<LoadedGLTF>();
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

void AssetManager::decodeImages(ThreadContext& threadCtx, const VmaAllocator allocator, DeletionQueue& bufferQueue) {
	assert(threadCtx.workQueueActive != nullptr);

	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	assert(queue && "[decodeImages] queue broken.");

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
				scene.images.push_back(*img);
			}
			else {
				// we failed to load, so lets give the slot a default white texture to not
				// completely break loading
				scene.images.push_back(ResourceManager::getCheckboardTex());
				fmt::print("gltf failed to load texture {}\n", image.name);
			}
		}

		queue->push(context);
		context->markJobComplete(GLTFJobType::DecodeImages);
	}
}

void AssetManager::buildSamplers(ThreadContext& threadCtx) {
	assert(threadCtx.workQueueActive != nullptr);

	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	assert(queue && "[buildSamplers] queue broken.");

	auto device = Backend::getDevice();

	auto gltfJobs = queue->collect();
	for (auto& context : gltfJobs) {
		auto& gltf = context->gltfAsset;
		auto& scene = *context->scene;

		for (fastgltf::Sampler& sampler : gltf.samplers) {
			VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .pNext = nullptr };
			sampl.maxLod = VK_LOD_CLAMP_NONE;
			sampl.minLod = 0.f;

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

			scene.samplers.push_back(newSampler);
		}

		queue->push(context);
		context->markJobComplete(GLTFJobType::BuildSamplers);
	}
}

void AssetManager::processMaterials(ThreadContext& threadCtx, const VmaAllocator allocator) {
	assert(threadCtx.workQueueActive != nullptr);

	auto& imageTable = ResourceManager::_globalImageTable;
	auto& resources = Engine::getState().getGPUResources();

	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	assert(queue && "[processMaterials] queue broken.");

	auto gltfJobs = queue->collect();

	size_t totalMaterialCount = 0;
	for (const auto& context : gltfJobs)
		totalMaterialCount += context->gltfAsset.materials.size();

	// create big material staging buffer
	AllocatedBuffer materialStaging = BufferUtils::createBuffer(
		sizeof(PBRMaterial) * totalMaterialCount,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
		allocator
	);

	resources.materialCount = static_cast<uint32_t>(totalMaterialCount);

	PBRMaterial* sceneMaterialConstants =
		static_cast<PBRMaterial*>(materialStaging.info.pMappedData);

	uint32_t globalDataIndex = 0;
	for (auto& context : gltfJobs) {
		if (!context->isJobComplete(GLTFJobType::DecodeImages) ||
			!context->isJobComplete(GLTFJobType::BuildSamplers)) {
			continue;
		}
		auto& gltf = context->gltfAsset;
		auto& scene = *context->scene;

		std::unordered_set<uint32_t> pushedLUTIndices;

		for (fastgltf::Material& mat : gltf.materials) {
			if (!isValidMaterial(mat, gltf)) {
				fmt::print("Warning: Skipping invalid material: {}\n", mat.name);
				continue;
			}

			auto newMat = std::make_shared<MaterialHandle>();
			newMat->instance = std::make_shared<InstanceData>();
			scene.materials.push_back(newMat);

			MaterialResources materialResources{
				.colorImage = ResourceManager::getWhiteImage(),
				.colorSampler = ResourceManager::getDefaultSamplerLinear(),
				.metalRoughImage = ResourceManager::getMetalRoughImage(),
				.metalRoughSampler = ResourceManager::getDefaultSamplerNearest(),
				.aoImage = ResourceManager::getAOImage(),
				.aoSampler = ResourceManager::getDefaultSamplerNearest(),
				.normalImage = ResourceManager::getNormalImage(),
				.normalSampler = ResourceManager::getDefaultSamplerLinear(),
				.emissiveImage = ResourceManager::getEmissiveImage(),
				.emissiveSampler = ResourceManager::getDefaultSamplerLinear(),
				.dataBuffer = materialStaging.buffer,
				.dataBufferOffset = globalDataIndex * sizeof(PBRMaterial),
				.dataBufferMapped = materialStaging.info.pMappedData,
			};

			auto getImageAndSampler = [&](auto& texRef, AllocatedImage& outImg, VkSampler& outSamp) {
				if (!texRef.has_value()) return;
				const auto& texture = gltf.textures[texRef->textureIndex];

				if (texture.imageIndex.has_value()) outImg = scene.images[texture.imageIndex.value()];
				if (texture.samplerIndex.has_value()) outSamp = scene.samplers[texture.samplerIndex.value()];
			};


			// Material constant factors
			PBRMaterial matConstants{};
			matConstants.colorFactor = glm::make_vec4(mat.pbrData.baseColorFactor.data());
			matConstants.metalRoughFactors = glm::vec2(mat.pbrData.metallicFactor, mat.pbrData.roughnessFactor);

			if (mat.normalTexture.has_value()) {
				getImageAndSampler(mat.normalTexture, materialResources.normalImage, materialResources.normalSampler);
				matConstants.normalScale = mat.normalTexture->scale;
			}

			if (mat.occlusionTexture.has_value()) {
				getImageAndSampler(mat.occlusionTexture, materialResources.aoImage, materialResources.aoSampler);
				matConstants.ambientOcclusion = mat.occlusionTexture->strength;
			}

			if (mat.emissiveTexture.has_value()) {
				getImageAndSampler(mat.emissiveTexture, materialResources.emissiveImage, materialResources.emissiveSampler);
				matConstants.emissiveStrength = mat.emissiveStrength;
			}

			if (mat.alphaMode == fastgltf::AlphaMode::Mask) {
				matConstants.alphaCutoff = (mat.alphaCutoff != 0.0f) ? mat.alphaCutoff : 0.5f;
			}

			// Material pass types
			MaterialPass passType = MaterialPass::Opaque;

			if (mat.alphaMode == fastgltf::AlphaMode::Blend) {
				fmt::print("Material name: {}\n", mat.name);
				passType = MaterialPass::Transparent;
			}

			newMat->passType = passType;


			// Image table loading
			uint32_t colorViewIdx = imageTable.pushCombined(materialResources.colorImage.imageView, materialResources.colorSampler);
			matConstants.albedoLUTIndex = colorViewIdx;
			uint32_t metalRoughViewIdx = imageTable.pushCombined(materialResources.metalRoughImage.imageView, materialResources.metalRoughSampler);
			matConstants.metalRoughLUTIndex = metalRoughViewIdx;
			uint32_t normalViewIdx = imageTable.pushCombined(materialResources.normalImage.imageView, materialResources.normalSampler);
			matConstants.normalLUTIndex = normalViewIdx;
			uint32_t aoViewIdx = imageTable.pushCombined(materialResources.aoImage.imageView, materialResources.aoSampler);
			matConstants.aoLUTIndex = aoViewIdx;


			ImageLUTEntry colorImgEntry{};
			colorImgEntry.combinedImageIndex = colorViewIdx,
			resources.addImageLUTEntry(colorImgEntry);

			ImageLUTEntry metalRoughImgEntry{};
			metalRoughImgEntry.combinedImageIndex = metalRoughViewIdx,
			resources.addImageLUTEntry(metalRoughImgEntry);

			ImageLUTEntry normalImgEntry{};
			normalImgEntry.combinedImageIndex = normalViewIdx;
			resources.addImageLUTEntry(normalImgEntry);

			ImageLUTEntry aoImgEntry{};
			aoImgEntry.combinedImageIndex = aoViewIdx,
			resources.addImageLUTEntry(aoImgEntry);


			sceneMaterialConstants[globalDataIndex] = matConstants;
			newMat->instance->materialIndex = globalDataIndex;
			globalDataIndex++;
		}

		queue->push(context);
		context->markJobComplete(GLTFJobType::ProcessMaterials);
	}

	// Create global accessible big ass material buffer
	AllocatedBuffer materialBuffer = BufferUtils::createGPUAddressBuffer(
		AddressBufferType::Material,
		resources.getAddressTable(),
		materialStaging.info.size,
		allocator);
	resources.addGPUBuffer(AddressBufferType::Material, materialBuffer);

	CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {
		VkBufferCopy copyRegion{};
		copyRegion.size = materialStaging.info.size;
		vkCmdCopyBuffer(cmd, materialStaging.buffer, materialBuffer.buffer, 1, &copyRegion);
	}, threadCtx.cmdPool, true);

	resources.updateAddressTableMapped(threadCtx.cmdPool);

	auto alloc = allocator;
	auto buffer = materialStaging;
	resources.getTempDeletionQueue().push_function([buffer, alloc]() mutable {
		BufferUtils::destroyBuffer(buffer, alloc);
	});
}

void AssetManager::processMeshes(ThreadContext& threadCtx) {
	assert(threadCtx.workQueueActive != nullptr);

	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	assert(queue && "[processMeshes] queue broken.");

	auto gltfJobs = queue->collect();
	auto& drawRanges = Engine::getState().getGPUResources().getDrawRanges();

	for (auto& context : gltfJobs) {
		if (!context->isJobComplete(GLTFJobType::ProcessMaterials))
			continue;

		auto& gltf = context->gltfAsset;
		auto& scene = *context->scene;
		auto& uploadCtx = context->uploadMeshCtx;

		for (fastgltf::Mesh& mesh : gltf.meshes) {
			auto newmesh = std::make_shared<MeshHandle>();
			newmesh->name = mesh.name;
			scene.meshes.push_back(newmesh);
			uploadCtx.meshHandles.push_back(newmesh);

			for (auto&& p : mesh.primitives) {
				auto newMat = std::make_shared<MaterialHandle>();
				newMat->instance = std::make_shared<InstanceData>();

				uint32_t globalVertexOffset = static_cast<uint32_t>(uploadCtx.globalVertices.size());
				uint32_t globalIndexOffset = static_cast<uint32_t>(uploadCtx.globalIndices.size());

				const auto& posAccessor = gltf.accessors[p.findAttribute("POSITION")->accessorIndex];
				uint32_t vertexCount = static_cast<uint32_t>(posAccessor.count);
				uploadCtx.globalVertices.resize(static_cast<size_t>(globalVertexOffset + vertexCount));

				fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor,
					[&](glm::vec3 v, size_t index) {
						assert(globalVertexOffset + index < uploadCtx.globalVertices.size());
						Vertex newvtx{};
						newvtx.position = v;
						newvtx.normal = glm::vec3(1.f, 0.f, 0.f);
						newvtx.color = glm::vec4(1.f);
						newvtx.uv = glm::vec2(0.f);
						uploadCtx.globalVertices[globalVertexOffset + index] = newvtx;
					});

				auto normals = p.findAttribute("NORMAL");
				if (normals != p.attributes.end()) {
					fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[normals->accessorIndex],
						[&](glm::vec3 v, size_t index) {
							uploadCtx.globalVertices[globalVertexOffset + index].normal = v;
						});
				}

				auto uv = p.findAttribute("TEXCOORD_0");
				if (uv != p.attributes.end()) {
					fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[uv->accessorIndex],
						[&](glm::vec2 v, size_t index) {
							uploadCtx.globalVertices[globalVertexOffset + index].uv = v;
						});
				}

				auto colors = p.findAttribute("COLOR_0");
				if (colors != p.attributes.end()) {
					fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[colors->accessorIndex],
						[&](glm::vec4 v, size_t index) {
							uploadCtx.globalVertices[globalVertexOffset + index].color = v;
						});
				}

				// === Index Buffer
				if (!p.indicesAccessor.has_value())
					continue;

				const auto& indexAccessor = gltf.accessors[p.indicesAccessor.value()];
				uint32_t indexCount = static_cast<uint32_t>(indexAccessor.count);

				uint32_t maxIndex = 0;
				uploadCtx.globalIndices.reserve(static_cast<size_t>(globalIndexOffset + indexCount));

				fastgltf::iterateAccessorWithIndex<uint32_t>(gltf, indexAccessor,
					[&](uint32_t idx, size_t /*i*/) {
						maxIndex = std::max(maxIndex, idx);
						uploadCtx.globalIndices.push_back(globalVertexOffset + idx);
					});

				assert(globalVertexOffset + maxIndex < uploadCtx.globalVertices.size() &&
					"Index buffer is referencing a vertex out of bounds!");

				GPUDrawRange range{
					.firstIndex = globalIndexOffset,
					.indexCount = indexCount,
					.vertexOffset = globalVertexOffset,
					.vertexCount = vertexCount
				};

				assert(uploadCtx.globalVertices.size() >= range.vertexOffset + range.vertexCount &&
					"Vertex buffer too small for range!");

				assert(uploadCtx.globalIndices.size() >= range.firstIndex + range.indexCount &&
					"Index buffer too small for range!");

				drawRanges.push_back(range);
				size_t rangeIdx = drawRanges.size() - 1;

				newMat->instance->drawRangeIndex = static_cast<uint32_t>(rangeIdx);

				// === Material setup ===
				if (p.materialIndex.has_value()) {
					uint32_t idx = static_cast<uint32_t>(p.materialIndex.value());
					if (idx < scene.materials.size()) {
						newMat->instance->materialIndex = scene.materials[idx]->instance->materialIndex;
						newMat->passType = scene.materials[idx]->passType;
					}
					else {
						fmt::print("Warning: material index {} out of bounds (total: {})\n", idx, scene.materials.size());
						newMat->instance->materialIndex = 0;
						newMat->passType = MaterialPass::Opaque;
					}
				}
				else {
					newMat->instance->materialIndex = 0;
					newMat->passType = MaterialPass::Opaque;
				}

				// === AABB Calculation ===
				glm::vec3 vmin = uploadCtx.globalVertices[globalVertexOffset].position;
				glm::vec3 vmax = vmin;
				for (uint32_t i = 0; i < vertexCount; ++i) {
					glm::vec3 pos = uploadCtx.globalVertices[globalVertexOffset + i].position;
					vmin = glm::min(vmin, pos);
					vmax = glm::max(vmax, pos);
				}

				newMat->instance->localAABB.vmin = vmin;
				newMat->instance->localAABB.vmax = vmax;
				newMat->instance->localAABB.origin = 0.5f * (vmin + vmax);
				newMat->instance->localAABB.extent = 0.5f * (vmax - vmin);
				newMat->instance->localAABB.sphereRadius = glm::length(newMat->instance->localAABB.extent);

				newmesh->materialHandles.push_back(newMat);
			}
		}

		fmt::print("[processMeshes] totals: meshes={}, verts={}, inds={}, ranges={}\n",
			uploadCtx.meshHandles.size(),
			uploadCtx.globalVertices.size(),
			uploadCtx.globalIndices.size(),
			drawRanges.size());

		assert(!drawRanges.empty() && !uploadCtx.globalVertices.empty() && !uploadCtx.globalIndices.empty() &&
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
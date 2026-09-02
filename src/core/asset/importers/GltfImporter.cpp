#include "pch.h"

#include "GltfImporter.h"
#include "../SourceGeometry.h"
#include "Material.h"
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include "../../../renderer/RendererDefinitions.h"

namespace
{
	constexpr auto kExtensions =
		fastgltf::Extensions::KHR_materials_emissive_strength |
		fastgltf::Extensions::KHR_lights_punctual |
		fastgltf::Extensions::EXT_meshopt_compression |
		fastgltf::Extensions::KHR_mesh_quantization |
		fastgltf::Extensions::KHR_materials_ior |
		fastgltf::Extensions::KHR_materials_specular |
		fastgltf::Extensions::KHR_materials_clearcoat |
		fastgltf::Extensions::KHR_materials_sheen |
		fastgltf::Extensions::KHR_materials_transmission |
		fastgltf::Extensions::KHR_materials_volume;
		//fastgltf::Extensions::KHR_texture_basisu;

	constexpr auto kOptions =
		fastgltf::Options::DontRequireValidAssetMember |
		fastgltf::Options::AllowDouble |
		fastgltf::Options::LoadGLBBuffers |
		fastgltf::Options::LoadExternalBuffers |
		fastgltf::Options::GenerateMeshIndices;

	bool IsTreeMaterial(const fastgltf::Material& mat)
	{
		if (mat.name.empty()) return false;
		std::string lower(mat.name.begin(), mat.name.end());
		std::transform(lower.begin(), lower.end(), lower.begin(),
			[](unsigned char c) { return std::tolower(c); });

		static constexpr std::string_view kTokens[] =
		{ "tree", "foliage", "leaf", "leaves", "ivy", "hedge", "bark", "flower" };

		for (auto t : kTokens)
			if (lower.find(t) != std::string_view::npos) return true;
		return false;
	}

	SamplerDesc ExtractSamplerDesc(const fastgltf::Sampler& sampler)
	{
		SamplerDesc desc{};
		const auto minFilter = sampler.minFilter.value_or(fastgltf::Filter::LinearMipMapLinear);

		desc.isLinear = true;
		switch (sampler.magFilter.value_or(fastgltf::Filter::Linear))
		{
		case fastgltf::Filter::Nearest:
		case fastgltf::Filter::NearestMipMapNearest:
		case fastgltf::Filter::NearestMipMapLinear:
			desc.isLinear = false;
			break;
		default: break;
		}

		switch (minFilter)
		{
		case fastgltf::Filter::NearestMipMapNearest:
		case fastgltf::Filter::LinearMipMapNearest:
		case fastgltf::Filter::NearestMipMapLinear:
		case fastgltf::Filter::LinearMipMapLinear:
			desc.isMipMapped = true;
			break;
		default:
			desc.isMipMapped = false;
			break;
		}

		desc.anisotropy = desc.isMipMapped ? RD::MAX_ANISOTROPY_LEVEL : 1.0f;
		return desc;
	}

	glm::mat4 NodeLocalTransform(const fastgltf::Node& node)
	{
		glm::mat4 local(1.0f);
		std::visit(fastgltf::visitor
			{
				[&](const fastgltf::math::fmat4x4& m)
				{
					local = glm::make_mat4x4(m.data());
				},
				[&](const fastgltf::TRS& trs)
				{
					const glm::vec3 t(trs.translation[0], trs.translation[1], trs.translation[2]);
					const glm::quat r(trs.rotation[3], trs.rotation[0], trs.rotation[1], trs.rotation[2]);
					const glm::vec3 s(trs.scale[0], trs.scale[1], trs.scale[2]);
					local = glm::translate(glm::mat4(1.f), t) * glm::toMat4(r) * glm::scale(glm::mat4(1.f), s);
				}
			}, node.transform);
		return local;
	}

	void ComputeWorldTransforms(const fastgltf::Asset& gltf, std::vector<glm::mat4>& out)
	{
		const size_t n = gltf.nodes.size();
		out.assign(n, glm::mat4(1.0f));

		std::vector<glm::mat4> local(n);
		std::vector<uint8_t>   isChild(n, 0);

		for (size_t i = 0; i < n; ++i)
		{
			local[i] = NodeLocalTransform(gltf.nodes[i]);
			for (auto c : gltf.nodes[i].children) isChild[c] = 1;
		}

		std::vector<std::pair<size_t, glm::mat4>> stack;
		for (size_t i = 0; i < n; ++i)
		{
			if (isChild[i]) continue;
			stack.push_back({ i, glm::mat4(1.0f) });

			while (!stack.empty())
			{
				auto [idx, parent] = stack.back();
				stack.pop_back();
				out[idx] = parent * local[idx];
				for (auto c : gltf.nodes[idx].children)
					stack.push_back({ c, out[idx] });
			}
		}
	}

	void GatherInstanceTransforms(
		const fastgltf::Asset& gltf,
		const fastgltf::Node& node,
		const glm::mat4& nodeWorld,
		SourceScene& out,
		std::vector<uint32_t>& outIndices)
	{
		auto find = [&](std::string_view name) -> const fastgltf::Accessor*
			{
				auto it = node.findInstancingAttribute(name);
				if (it == node.instancingAttributes.end()) return nullptr;
				return &gltf.accessors[it->accessorIndex];
			};

		const fastgltf::Accessor* tAcc = find("TRANSLATION");
		const fastgltf::Accessor* rAcc = find("ROTATION");
		const fastgltf::Accessor* sAcc = find("SCALE");

		size_t count = 0;
		if (tAcc) count = std::max(count, tAcc->count);
		if (rAcc) count = std::max(count, rAcc->count);
		if (sAcc) count = std::max(count, sAcc->count);

		if (count == 0)
		{
			outIndices.push_back(out.AddTransform(nodeWorld));
			return;
		}

		std::vector<glm::vec3> translations(count, glm::vec3(0.0f));
		std::vector<glm::quat> rotations(count, glm::quat(1, 0, 0, 0));
		std::vector<glm::vec3> scales(count, glm::vec3(1.0f));

		if (tAcc)
			fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, *tAcc,
				[&](glm::vec3 v, size_t i) { translations[i] = v; });

		if (rAcc)
			fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, *rAcc,
				[&](glm::vec4 v, size_t i) { rotations[i] = glm::quat(v.w, v.x, v.y, v.z); });

		if (sAcc)
			fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, *sAcc,
				[&](glm::vec3 v, size_t i) { scales[i] = v; });

		outIndices.reserve(count);
		for (size_t i = 0; i < count; ++i)
		{
			const glm::mat4 local =
				glm::translate(glm::mat4(1.f), translations[i]) *
				glm::toMat4(rotations[i]) *
				glm::scale(glm::mat4(1.f), scales[i]);

			outIndices.push_back(out.AddTransform(nodeWorld * local));
		}
	}
}

bool ImportGltf(const std::filesystem::path& file, const ImportOptions& opts, SourceScene& out)
{
	auto data = fastgltf::GltfDataBuffer::FromPath(file);
	if (data.error() != fastgltf::Error::None)
	{
		fmt::println("[Import] Read failed {}: {}", file.string(),
			fastgltf::getErrorMessage(data.error()));
		return false;
	}

	const auto basePath = file.parent_path();

	fastgltf::Parser parser(kExtensions);
	auto result = parser.loadGltf(data.get(), basePath, kOptions);
	if (result.error() != fastgltf::Error::None)
	{
		fmt::println("[Import] Parse failed {}: {}", file.string(),
			fastgltf::getErrorMessage(result.error()));
		return false;
	}

	fastgltf::Asset gltf = std::move(result.get());

	std::vector<uint8_t> srgbImage(gltf.images.size(), 0);
	std::vector<uint8_t> normalImage(gltf.images.size(), 0);
	{
		auto markSRGB = [&](const fastgltf::TextureInfo& info)
			{
				if (info.textureIndex >= gltf.textures.size()) return;
				const auto& tex = gltf.textures[info.textureIndex];
				if (tex.imageIndex.has_value() && *tex.imageIndex < srgbImage.size())
					srgbImage[*tex.imageIndex] = 1;
			};

		for (const auto& mat : gltf.materials)
		{
			if (mat.pbrData.baseColorTexture.has_value()) markSRGB(*mat.pbrData.baseColorTexture);
			if (mat.emissiveTexture.has_value())          markSRGB(*mat.emissiveTexture);

			if (mat.normalTexture.has_value())
			{
				const auto& tex = gltf.textures[mat.normalTexture->textureIndex];
				if (tex.imageIndex.has_value() && *tex.imageIndex < normalImage.size())
					normalImage[*tex.imageIndex] = 1;
			}
		}
	}

	out.images.reserve(gltf.images.size());
	for (size_t i = 0; i < gltf.images.size(); ++i)
	{
		auto& src = gltf.images[i];
		SourceImage img{};
		img.isSRGB = (srgbImage[i] != 0);
		img.isNormalMap = (normalImage[i] != 0);
		img.name = src.name.empty()
			? fmt::format("tex_{}_{}", file.stem().string(), i)
			: std::string(src.name);

		std::visit(fastgltf::visitor
			{
				[&](fastgltf::sources::Array& arr)
				{
					const auto* p = reinterpret_cast<const uint8_t*>(arr.bytes.data());
					img.encodedBytes.assign(p, p + arr.bytes.size());
				},
				[&](fastgltf::sources::URI& uri)
				{
					ASSERT(uri.uri.isLocalPath());
					img.filePath = basePath / std::string(uri.uri.path());
					if (src.name.empty()) img.name = img.filePath.filename().string();
				},

			[&](fastgltf::sources::BufferView& bv)
			{
				auto& view = gltf.bufferViews[bv.bufferViewIndex];
				auto& buf = gltf.buffers[view.bufferIndex];

				auto take = [&](const void* base, size_t total)
				{
					ASSERT(view.byteOffset + view.byteLength <= total);
					const auto* p = static_cast<const uint8_t*>(base) + view.byteOffset;
					img.encodedBytes.assign(p, p + view.byteLength);
				};

				std::visit(fastgltf::visitor
				{
					[&](fastgltf::sources::Array& a) { take(a.bytes.data(), a.bytes.size()); },
					[&](fastgltf::sources::Vector& v) { take(v.bytes.data(), v.bytes.size()); },
					[&](fastgltf::sources::ByteView& b) { take(b.bytes.data(), b.bytes.size()); },
					[&](auto&)
					{
						fmt::println("[Import] Image {} buffer source unhandled", img.name);
					}
				}, buf.data);
			},[](auto&) {}
		}, src.data);

		out.images.push_back(std::move(img));
	}

	out.samplers.reserve(gltf.samplers.size());
	for (const auto& s : gltf.samplers)
		out.samplers.push_back(ExtractSamplerDesc(s));

	out.materials.emplace_back(MaterialDesc{
		.shadingModel = SHADING_MODEL_STANDARD,
		.flags = MATERIAL_FLAG_CASTS_SHADOWS,
		.passType = static_cast<uint32_t>(MaterialPass::Opaque) });

	for (const auto& mat : gltf.materials)
	{
		MaterialDesc desc{};
		desc.flags |= MATERIAL_FLAG_CASTS_SHADOWS;
		desc.passType = static_cast<uint32_t>(MaterialPass::Opaque);
		desc.shadingModel = SHADING_MODEL_STANDARD;

		desc.colorFactor = glm::make_vec4(mat.pbrData.baseColorFactor.data());
		desc.metalRoughFactors = { mat.pbrData.metallicFactor, mat.pbrData.roughnessFactor };
		desc.emissiveColor = glm::make_vec3(mat.emissiveFactor.data());
		desc.emissiveStrength = mat.emissiveStrength;

		auto resolve = [&](const fastgltf::TextureInfo& info, uint32_t& tex, uint32_t& samp)
			{
				const auto& t = gltf.textures[info.textureIndex];
				if (t.imageIndex.has_value())   tex = static_cast<uint32_t>(*t.imageIndex);
				if (t.samplerIndex.has_value()) samp = static_cast<uint32_t>(*t.samplerIndex);
			};

		if (mat.pbrData.baseColorTexture.has_value())
		{
			resolve(*mat.pbrData.baseColorTexture, desc.albedoTexIdx, desc.albedoSamplerIdx);
		}
		if (mat.pbrData.metallicRoughnessTexture.has_value())
			resolve(*mat.pbrData.metallicRoughnessTexture, desc.metalRoughTexIdx, desc.metalRoughSampIdx);

		if (mat.normalTexture.has_value() && mat.normalTexture->textureIndex < gltf.textures.size())
		{
			resolve(*mat.normalTexture, desc.normalTexIdx, desc.normalSamplerIdx);
			desc.normalScale = mat.normalTexture->scale;
			desc.flags |= MATERIAL_FLAG_HAS_NORMAL_MAP;
		}
		if (mat.emissiveTexture.has_value())
			resolve(*mat.emissiveTexture, desc.emissiveTexIdx, desc.emissiveSampIdx);

		const bool isTree = IsTreeMaterial(mat);

		if (mat.alphaMode == fastgltf::AlphaMode::Mask)
		{
			desc.alphaCutoff = mat.alphaCutoff != 0.0f ? mat.alphaCutoff : 0.5f;
			desc.flags |= MATERIAL_FLAG_ALPHA_MASKED;
		}
		else if (mat.alphaMode == fastgltf::AlphaMode::Blend)
		{
			if (isTree)
			{
				desc.alphaCutoff = 0.5f;
				desc.flags |= MATERIAL_FLAG_ALPHA_MASKED;
			}
			else
			{
				desc.passType = static_cast<uint32_t>(MaterialPass::Transparent);
				desc.flags &= ~MATERIAL_FLAG_CASTS_SHADOWS;
			}
		}

		if (isTree)
			desc.flags |= MATERIAL_FLAG_IS_TREE;

		if (mat.doubleSided)
			desc.flags |= MATERIAL_FLAG_DOUBLE_SIDED;

		desc.ior = mat.ior;

		if (mat.specular)
		{
			desc.specularFactor = mat.specular->specularFactor;

			if (mat.specular->specularTexture.has_value())
				resolve(*mat.specular->specularTexture,
					desc.metalRoughTexIdx, desc.metalRoughSampIdx);
		}

		if (mat.volume)
		{
			desc.thicknessFactor = mat.volume->thicknessFactor;
			desc.attenuationColor = glm::make_vec3(mat.volume->attenuationColor.data());
			desc.attenuationDistance = std::isfinite(mat.volume->attenuationDistance)
				? mat.volume->attenuationDistance
				: 0.0f;
		}

		if (mat.clearcoat && mat.clearcoat->clearcoatFactor > 0.0f)
		{
			desc.shadingModel = SHADING_MODEL_CLEARCOAT;
			desc.clearcoatFactor = mat.clearcoat->clearcoatFactor;
			desc.clearcoatRough = mat.clearcoat->clearcoatRoughnessFactor;
		}
		else if (mat.sheen)
		{
			desc.shadingModel = SHADING_MODEL_SHEEN;
			desc.sheenColor = glm::make_vec3(mat.sheen->sheenColorFactor.data());
			desc.sheenRough = mat.sheen->sheenRoughnessFactor;
		}
		else if (mat.transmission && mat.transmission->transmissionFactor > 0.0f)
		{
			desc.shadingModel = SHADING_MODEL_TRANSMISSION;
			desc.transmissionFactor = mat.transmission->transmissionFactor;
			desc.flags |= MATERIAL_FLAG_TRANSMISSIVE;
		}

		out.materials.push_back(desc);
	}

	std::vector<glm::mat4> world;
	ComputeWorldTransforms(gltf, world);

	out.nodes.reserve(gltf.nodes.size());
	for (size_t nodeIdx = 0; nodeIdx < gltf.nodes.size(); ++nodeIdx)
	{
		const auto& node = gltf.nodes[nodeIdx];

		if (node.lightIndex.has_value() && *node.lightIndex < gltf.lights.size())
		{
			const auto& src = gltf.lights[*node.lightIndex];

			SourceLight light{};
			light.color = glm::vec3(
				static_cast<float>(src.color[0]),
				static_cast<float>(src.color[1]),
				static_cast<float>(src.color[2]));
			light.intensity = static_cast<float>(src.intensity);
			light.range = static_cast<float>(src.range.value_or(0.0));
			light.innerConeAngle = static_cast<float>(src.innerConeAngle.value_or(0.0));
			light.outerConeAngle = static_cast<float>(src.outerConeAngle.value_or(0.7853982));
			light.type =
				src.type == fastgltf::LightType::Point ? 0u :
				src.type == fastgltf::LightType::Spot ? 1u : 2u;
			light.transformIndex = out.AddTransform(world[nodeIdx]);

			out.lights.push_back(light);
		}

		if (!node.meshIndex.has_value()) continue;

		SourceNode outNode{};
		GatherInstanceTransforms(gltf, node, world[nodeIdx], out, outNode.transformIndices);

		for (const auto& prim : gltf.meshes[*node.meshIndex].primitives)
		{
			auto posAttr = prim.findAttribute("POSITION");
			if (posAttr == prim.attributes.end())  continue;
			if (!prim.indicesAccessor.has_value()) continue;

			const auto& posAcc = gltf.accessors[posAttr->accessorIndex];
			const auto& idxAcc = gltf.accessors[*prim.indicesAccessor];

			SourcePrimitive sp{};
			sp.vertices.resize(posAcc.count);
			sp.indices.resize(idxAcc.count);

			if (prim.materialIndex.has_value())
				sp.materialIdx = static_cast<uint32_t>(*prim.materialIndex) + 1;

			const bool wantsTangents =
				(out.materials[sp.materialIdx].flags & MATERIAL_FLAG_HAS_NORMAL_MAP) != 0;

			SourceAttribs attribs{};
			if (wantsTangents)
			{
				attribs.normals.resize(posAcc.count);
				attribs.uvs.resize(posAcc.count);
			}

			fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAcc,
				[&](glm::vec3 v, size_t i) { sp.vertices[i].position = v; });

			bool hasNormals = false, hasTangents = false;

			if (auto it = prim.findAttribute("NORMAL"); it != prim.attributes.end())
			{
				hasNormals = true;
				fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[it->accessorIndex],
					[&](glm::vec3 v, size_t i)
					{
						EncodeOctahedral_Normal(sp.vertices[i], v);
						if (wantsTangents) attribs.normals[i] = v;
					});
			}
			if (auto it = prim.findAttribute("TANGENT"); it != prim.attributes.end())
			{
				hasTangents = true;
				fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[it->accessorIndex],
					[&](glm::vec4 v, size_t i) { EncodeOctahedral_Tangent(sp.vertices[i], v); });
			}
			if (auto it = prim.findAttribute("TEXCOORD_0"); it != prim.attributes.end())
				fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[it->accessorIndex],
					[&](glm::vec2 v, size_t i)
					{
						const float u = v.x;
						const float w = opts.flipUVs ? 1.0f - v.y : v.y;
						sp.vertices[i].uvX = FloatToHalfBits(u);
						sp.vertices[i].uvY = FloatToHalfBits(w);
						if (wantsTangents) attribs.uvs[i] = glm::vec2(u, w);
					});

			if (auto it = prim.findAttribute("COLOR_0"); it != prim.attributes.end())
				fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[it->accessorIndex],
					[&](glm::vec4 v, size_t i) { EncodeRGBA8(sp.vertices[i], v); });

			fastgltf::iterateAccessorWithIndex<uint32_t>(gltf, idxAcc,
				[&](uint32_t idx, size_t j) { sp.indices[j] = idx; });

			if (!hasTangents && hasNormals && wantsTangents)
				GenerateTangents(sp, attribs);

			outNode.primitives.push_back(std::move(sp));
		}

		if (!outNode.primitives.empty())
			out.nodes.push_back(std::move(outNode));
	}

	return out.IsValid();
}

#include "pch.h"

#include "SceneSource.h"
#include "importers/GltfImporter.h"

namespace
{
	using ImportFn = bool(*)(const std::filesystem::path&, const ImportOptions&, SourceScene&);

	struct FormatEntry { const char* ext; ImportFn fn; };

	constexpr FormatEntry kFormats[] =
	{
		{ ".gltf", &ImportGltf },
		{ ".glb",  &ImportGltf }
	};
}

bool ImportScene(const std::filesystem::path& file, const ImportOptions& opts, SourceScene& out)
{
	std::string ext = file.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(),
		[](unsigned char c) { return std::tolower(c); });

	for (const auto& entry : kFormats)
	{
		if (ext != entry.ext) continue;

		if (!entry.fn(file, opts, out)) return false;

		if (opts.importScale != 1.0f)
		{
			for (auto& node : out.nodes)
				for (auto& prim : node.primitives)
					for (auto& v : prim.vertices)
						v.position *= opts.importScale;

			for (auto& light : out.lights)
				light.range *= opts.importScale;

			for (auto& m : out.transforms)
				m[3] = glm::vec4(glm::vec3(m[3]) * opts.importScale, 1.0f);
		}
		return true;
	}

	fmt::println("[Import] No importer for extension: {}", ext);
	return false;
}

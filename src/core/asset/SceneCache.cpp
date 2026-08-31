#include "pch.h"

#include "SceneCache.h"
#include <fstream>

namespace
{
	constexpr uint32_t kMagic = 0x4E454353;
	constexpr uint32_t kVersion = 2;
	constexpr uint64_t kMaxElements = 1ull << 32;

	inline static const std::string CacheDir = "res/cache/";

	struct CacheHeader
	{
		uint32_t magic = kMagic;
		uint32_t version = kVersion;
		uint32_t vertexStride = sizeof(Vertex);
		uint32_t meshletStride = sizeof(Meshlet);
		uint32_t meshDescSize = sizeof(MeshDesc);
		uint32_t textureMipSize = sizeof(TextureMipDesc);
		uint32_t optionsHash = 0;
		uint64_t sourceSize = 0;
		int64_t  sourceTime = 0;
	};

	uint32_t HashOptions(const ImportOptions& opts)
	{
		uint32_t h = 2166136261u;
		const auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };

		uint32_t scaleBits = 0;
		std::memcpy(&scaleBits, &opts.importScale, sizeof(uint32_t));
		mix(scaleBits);
		mix(opts.flipUVs ? 1u : 0u);
		return h;
	}

	bool SourceStamp(const std::filesystem::path& src, uint64_t& size, int64_t& time)
	{
		std::error_code ec;
		size = std::filesystem::file_size(src, ec);
		if (ec) return false;

		const auto t = std::filesystem::last_write_time(src, ec);
		if (ec) return false;

		time = t.time_since_epoch().count();
		return true;
	}

	template<typename T>
	void WritePod(std::ostream& s, const T& v)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		s.write(reinterpret_cast<const char*>(&v), sizeof(T));
	}

	template<typename T>
	bool ReadPod(std::istream& s, T& v)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		return static_cast<bool>(s.read(reinterpret_cast<char*>(&v), sizeof(T)));
	}

	template<typename T>
	void WriteVec(std::ostream& s, const std::vector<T>& v)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		const uint64_t n = v.size();
		WritePod(s, n);
		if (n) s.write(reinterpret_cast<const char*>(v.data()), n * sizeof(T));
	}

	template<typename T>
	bool ReadVec(std::istream& s, std::vector<T>& v)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		uint64_t n = 0;
		if (!ReadPod(s, n) || n > kMaxElements) return false;
		v.resize(static_cast<size_t>(n));
		if (n == 0) return true;
		return static_cast<bool>(s.read(reinterpret_cast<char*>(v.data()), n * sizeof(T)));
	}

	void WriteStr(std::ostream& s, const std::string& str)
	{
		const uint64_t n = str.size();
		WritePod(s, n);
		if (n) s.write(str.data(), n);
	}

	bool ReadStr(std::istream& s, std::string& str)
	{
		uint64_t n = 0;
		if (!ReadPod(s, n) || n > (1ull << 20)) return false;
		str.resize(static_cast<size_t>(n));
		if (n == 0) return true;
		return static_cast<bool>(s.read(str.data(), n));
	}
}

std::filesystem::path SceneCache::PathFor(
	const std::filesystem::path& source, const ImportOptions& opts)
{
	const uint64_t h = std::hash<std::string>{}(source.generic_string())
		^ (static_cast<uint64_t>(HashOptions(opts)) << 32);

	return std::filesystem::path(CacheDir) /
		fmt::format("{}_{:016x}.scb", source.stem().string(), h);
}

bool SceneCache::Load(
	const std::filesystem::path& source,
	const ImportOptions& opts,
	SceneUploadBatch& outBatch)
{
	const auto path = PathFor(source, opts);

	std::ifstream in(path, std::ios::binary);
	if (!in) return false;

	CacheHeader header{};
	if (!ReadPod(in, header)) return false;

	CacheHeader expected{};
	expected.optionsHash = HashOptions(opts);
	if (!SourceStamp(source, expected.sourceSize, expected.sourceTime)) return false;

	if (header.magic != expected.magic ||
		header.version != expected.version ||
		header.vertexStride != expected.vertexStride ||
		header.meshletStride != expected.meshletStride ||
		header.meshDescSize != expected.meshDescSize ||
		header.optionsHash != expected.optionsHash ||
		header.sourceSize != expected.sourceSize ||
		header.textureMipSize != expected.textureMipSize ||
		header.sourceTime != expected.sourceTime)
		return false;

	uint64_t textureCount = 0;
	if (!ReadPod(in, textureCount) || textureCount > (1ull << 20)) return false;

	outBatch.textures.clear();
	outBatch.textures.resize(static_cast<size_t>(textureCount));

	for (auto& tex : outBatch.textures)
	{
		uint32_t format = 0;
		uint8_t  srgb = 0;

		if (!ReadStr(in, tex.debugName))  return false;
		if (!ReadPod(in, tex.width))      return false;
		if (!ReadPod(in, tex.height))     return false;
		if (!ReadPod(in, format))         return false;
		if (!ReadPod(in, srgb))           return false;
		if (!ReadVec(in, tex.mips))       return false;
		if (!ReadVec(in, tex.pixelData))  return false;

		tex.format = static_cast<TextureFormat>(format);
		tex.isSRGB = (srgb != 0);
	}

	uint32_t sceneID = 0;
	uint32_t lifetime = 0;

	if (!ReadPod(in, sceneID))                     return false;
	if (!ReadStr(in, outBatch.sceneName))          return false;
	if (!ReadPod(in, lifetime))                    return false;
	if (!ReadPod(in, outBatch.virtualInstance))    return false;

	if (!ReadVec(in, outBatch.vertices))           return false;
	if (!ReadVec(in, outBatch.indices))            return false;
	if (!ReadVec(in, outBatch.meshlets))           return false;
	if (!ReadVec(in, outBatch.meshletVertices))    return false;
	if (!ReadVec(in, outBatch.meshletTriangles))   return false;
	if (!ReadVec(in, outBatch.samplers))           return false;
	if (!ReadVec(in, outBatch.materials))          return false;
	if (!ReadVec(in, outBatch.materialFlags))      return false;
	if (!ReadVec(in, outBatch.meshes))             return false;
	if (!ReadVec(in, outBatch.instances))          return false;
	if (!ReadVec(in, outBatch.nodeTransforms))     return false;
	if (!ReadVec(in, outBatch.localToNodeSlot))    return false;
	if (!ReadVec(in, outBatch.lights))             return false;
	if (!ReadVec(in, outBatch.lightTransforms))    return false;

	outBatch.sceneID = static_cast<ModelID>(sceneID);
	outBatch.lifetime = static_cast<RD::ResourceLifetime>(lifetime);

	fmt::println("[SceneCache] Hit: {}", path.filename().string());
	return true;
}

bool SceneCache::Store(
	const std::filesystem::path& source,
	const ImportOptions& opts,
	const SceneUploadBatch& batch)
{
	std::error_code ec;
	std::filesystem::create_directories(CacheDir, ec);

	const auto finalPath = PathFor(source, opts);
	const auto tempPath = std::filesystem::path(finalPath).concat(".tmp");

	{
		std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
		if (!out) return false;

		CacheHeader header{};
		header.optionsHash = HashOptions(opts);
		if (!SourceStamp(source, header.sourceSize, header.sourceTime)) return false;
		WritePod(out, header);

		WritePod(out, static_cast<uint64_t>(batch.textures.size()));
		for (const auto& tex : batch.textures)
		{
			WriteStr(out, tex.debugName);
			WritePod(out, tex.width);
			WritePod(out, tex.height);
			WritePod(out, static_cast<uint32_t>(tex.format));
			WritePod(out, static_cast<uint8_t>(tex.isSRGB ? 1 : 0));
			WriteVec(out, tex.mips);
			WriteVec(out, tex.pixelData);
		}

		WritePod(out, static_cast<uint32_t>(batch.sceneID));
		WriteStr(out, batch.sceneName);
		WritePod(out, static_cast<uint32_t>(batch.lifetime));
		WritePod(out, batch.virtualInstance);

		WriteVec(out, batch.vertices);
		WriteVec(out, batch.indices);
		WriteVec(out, batch.meshlets);
		WriteVec(out, batch.meshletVertices);
		WriteVec(out, batch.meshletTriangles);
		WriteVec(out, batch.samplers);
		WriteVec(out, batch.materials);
		WriteVec(out, batch.materialFlags);
		WriteVec(out, batch.meshes);
		WriteVec(out, batch.instances);
		WriteVec(out, batch.nodeTransforms);
		WriteVec(out, batch.localToNodeSlot);
		WriteVec(out, batch.lights);
		WriteVec(out, batch.lightTransforms);

		if (!out.good()) return false;
	}

	std::filesystem::remove(finalPath, ec);
	std::filesystem::rename(tempPath, finalPath, ec);
	return !ec;
}

#pragma once

#include "AssetUploadTypes.h"
#include "SceneSource.h"

namespace SceneCache
{
	std::filesystem::path PathFor(const std::filesystem::path& source, const ImportOptions& opts);

	bool Load(
		const std::filesystem::path& source,
		const ImportOptions& opts,
		SceneUploadBatch& outBatch);

	bool Store(
		const std::filesystem::path& source,
		const ImportOptions& opts,
		const SceneUploadBatch& batch);
}

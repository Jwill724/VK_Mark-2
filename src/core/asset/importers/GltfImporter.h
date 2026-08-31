#pragma once

#include "../SceneSource.h"

bool ImportGltf(const std::filesystem::path& file, const ImportOptions& opts, SourceScene& out);

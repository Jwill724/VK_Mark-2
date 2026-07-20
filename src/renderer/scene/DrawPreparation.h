#pragma once

#include "Core.h"
#include <vector>

#include "renderer/RendererDefinitions.h"
namespace RD = RendererDefinitions;

class FrameContext;
struct LocalLight;
class Device;
class Allocator;
struct Mesh;
struct MeshLODs;
struct InstanceState;
struct VirtualInstance;
struct InstanceInput;
struct BinTableBuild;
struct DrawBinKeys;
enum class ModelID;
struct ModelAsset;
class BindlessBDATable;

namespace DrawPreparation
{
	bool SyncInstanceInputs(
		InstanceState& vs,
		const std::vector<VirtualInstance>& virtualInstances,
		const std::unordered_map<ModelID, std::shared_ptr<ModelAsset>>& loaded,
		const std::vector<Mesh>& meshData,
		const std::vector<MeshLODs>& meshLods,
		const std::vector<glm::mat4>& transforms,
		const std::vector<uint32_t>&  materialFlags);

	BinTableBuild BuildDrawBinTable(const std::vector<InstanceInput>& instances);

	void UploadGPUBuffersForFrame(
		FrameContext&                     frameCtx,
		BindlessBDATable&                 globalBDATable,
		const DrawBinKeys&                drawBinKeys,
		Device&                           device,
		Allocator&                        allocator,
		const std::vector<InstanceInput>& instanceInputs,
		const std::vector<glm::mat4>&     transforms,
		const std::vector<LocalLight>&    lights,
		bool                              isTemporalValid);
}

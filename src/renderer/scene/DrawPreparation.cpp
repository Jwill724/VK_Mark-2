#include "pch.h"

#include "DrawPreparation.h"
#include "engine/Engine.h"
#include "utils/BufferUtils.h"

struct CombinedUploadPlan {
	// frame-local
	size_t visOff = 0, visSize = 0; // visible instances
	size_t indOff = 0, indSize = 0; // indirect draws
	// per-frame address table (frameCtx.addressTableBuffer)
	size_t fAddrOff = 0, fAddrSize = 0;

	// global address table and transforms
	size_t gAddrOff = 0, gAddrSize = 0;
	size_t transformOff = 0, transformSize = 0;
};

struct TransparentEntry {
	GPUInstance instance;
	AABB aabb; // Need index into worldaabbs for transparent depth sort
};

// All render data is reset prior to this each frame
void DrawPreparation::buildAndSortIndirectDraws(
	FrameContext& frameCtx,
	const std::vector<GPUMeshData>& meshes,
	const std::vector<AABB>& worldAABBs,
	const glm::vec4 cameraPos)
{
	// Partition visible instances, while remembering their original indices
	std::vector<GPUInstance> opaqueInstances;
	std::vector<TransparentEntry> transparentEntries;

	opaqueInstances.reserve(frameCtx.visibleInstances.size());
	transparentEntries.reserve(frameCtx.visibleInstances.size());

	// === BATCH OPAQUE INSTANCES ===
	std::unordered_map<OpaqueBatchKey, std::vector<uint32_t>, OpaqueBatchKeyHash> opaqueBatches;

	// Separate pass types
	for (uint32_t i = 0; i < frameCtx.visibleInstances.size(); ++i) {
		const auto& inst = frameCtx.visibleInstances[i];
		if (static_cast<MaterialPass>(inst.passType) == MaterialPass::Opaque) {
			uint32_t opaqueIndex = static_cast<uint32_t>(opaqueInstances.size());
			opaqueInstances.push_back(inst);
			const OpaqueBatchKey key{ inst.meshID, inst.materialID };
			opaqueBatches[key].push_back(opaqueIndex);
		}
		else {
			transparentEntries.push_back({ inst, worldAABBs[i] });
		}
	}

	frameCtx.indirectDraws.reserve(opaqueBatches.size() + transparentEntries.size());

	// Rebuild instances in draw order
	frameCtx.visibleInstances.clear();
	frameCtx.visibleInstances.reserve(opaqueInstances.size() + transparentEntries.size());

	// Opaque first
	frameCtx.opaqueRange.first = 0;
	for (const auto& [key, instanceIndices] : opaqueBatches) {
		const GPUMeshData& mesh = meshes[key.meshID];

		ASSERT(mesh.firstIndex + mesh.indexCount <= frameCtx.drawDataPC.totalIndexCount &&
			"[DrawPrep] Opaque draws would read past end of index buffer.");
		ASSERT(mesh.vertexOffset + mesh.vertexCount <= frameCtx.drawDataPC.totalVertexCount &&
			"[DrawPrep] Opaque draws would read past end of vertex buffer.");

		VkDrawIndexedIndirectCommand cmd {
			.indexCount = mesh.indexCount,
			.instanceCount = static_cast<uint32_t>(instanceIndices.size()),
			.firstIndex = mesh.firstIndex,
			.vertexOffset = static_cast<int32_t>(mesh.vertexOffset),
			.firstInstance = frameCtx.opaqueRange.first + frameCtx.opaqueRange.visibleCount
		};

		frameCtx.indirectDraws.emplace_back(cmd);
		for (uint32_t idx : instanceIndices)
			frameCtx.visibleInstances.emplace_back(opaqueInstances[idx]);

		frameCtx.opaqueRange.visibleCount += cmd.instanceCount;
	}

	// === SORT AND BUILD TRANSPARENT ===
	if (!transparentEntries.empty()) {
		frameCtx.transparentRange.first = frameCtx.opaqueRange.visibleCount;
		frameCtx.transparentRange.visibleCount = static_cast<uint32_t>(transparentEntries.size());

		const glm::vec3 camPos = glm::vec3(cameraPos);
		std::sort(transparentEntries.begin(), transparentEntries.end(),
			[&](const TransparentEntry& a, const TransparentEntry& b) {
				return glm::length(a.aabb.origin - camPos) > glm::length(b.aabb.origin - camPos);
		});

		for (uint32_t i = 0; i < transparentEntries.size(); ++i) {
			const auto& entry = transparentEntries[i];
			const GPUMeshData& mesh = meshes[entry.instance.meshID];

			ASSERT(mesh.firstIndex + mesh.indexCount <= frameCtx.drawDataPC.totalIndexCount &&
				"[DrawPrep] Transparent draws would read past end of index buffer.");
			ASSERT(mesh.vertexOffset + mesh.vertexCount <= frameCtx.drawDataPC.totalVertexCount &&
				"[DrawPrep] Transparent draws would read past end of vertex buffer.");

			VkDrawIndexedIndirectCommand cmd {
				.indexCount = mesh.indexCount,
				.instanceCount = 1,
				.firstIndex = mesh.firstIndex,
				.vertexOffset = static_cast<int32_t>(mesh.vertexOffset),
				.firstInstance = frameCtx.transparentRange.first + i
			};

			frameCtx.indirectDraws.push_back(cmd);
			frameCtx.visibleInstances.push_back(entry.instance);
		}
	}
}

inline static CombinedUploadPlan stageCombinedUploads(FrameContext& frame,
	const GPUAddressTable& globalAddrTable,
	const std::vector<glm::mat4>& transforms,
	VmaAllocator alloc)
{
	CombinedUploadPlan plan{};
	auto* mapped = static_cast<uint8_t*>(frame.combinedGPUStaging.info.pMappedData);
	const size_t cap = frame.combinedGPUStaging.info.size;
	const size_t addrBytes = sizeof(GPUAddressTable);

	// visible instances
	plan.visSize = frame.visibleInstances.size() * sizeof(GPUInstance);
	plan.visOff = BufferUtils::reserveStaging(frame.stagingHead, cap, plan.visSize);
	memcpy(mapped + plan.visOff, frame.visibleInstances.data(), plan.visSize);
	BufferUtils::flushStagingRange(frame.combinedGPUStaging.allocation, plan.visOff, plan.visSize, alloc);

	// indirect
	plan.indSize = frame.indirectDraws.size() * sizeof(VkDrawIndexedIndirectCommand);
	plan.indOff = BufferUtils::reserveStaging(frame.stagingHead, cap, plan.indSize);
	memcpy(mapped + plan.indOff, frame.indirectDraws.data(), plan.indSize);
	BufferUtils::flushStagingRange(frame.combinedGPUStaging.allocation, plan.indOff, plan.indSize, alloc);

	// frame address table
	plan.fAddrSize = addrBytes;
	plan.fAddrOff = BufferUtils::reserveStaging(frame.stagingHead, cap, plan.fAddrSize);
	memcpy(mapped + plan.fAddrOff, &frame.addressTable, plan.fAddrSize);
	BufferUtils::flushStagingRange(frame.combinedGPUStaging.allocation, plan.fAddrOff, plan.fAddrSize, alloc);

	// transforms + global address table
	if (frame.transformsBufferUploadNeeded) {
		plan.transformSize = transforms.size() * sizeof(glm::mat4);
		plan.transformOff = BufferUtils::reserveStaging(frame.stagingHead, cap, plan.transformSize);
		memcpy(mapped + plan.transformOff, transforms.data(), plan.transformSize);
		BufferUtils::flushStagingRange(frame.combinedGPUStaging.allocation, plan.transformOff, plan.transformSize, alloc);

		plan.gAddrSize = addrBytes;
		plan.gAddrOff = BufferUtils::reserveStaging(frame.stagingHead, cap, plan.gAddrSize);
		memcpy(mapped + plan.gAddrOff, &globalAddrTable, plan.gAddrSize);
		BufferUtils::flushStagingRange(frame.combinedGPUStaging.allocation, plan.gAddrOff, plan.gAddrSize, alloc);
	}

	return plan;
}

void DrawPreparation::uploadGPUBuffersForFrame(
	FrameContext& frameCtx,
	GPUResources& gpuResources,
	const std::vector<glm::mat4>& transforms,
	GPUQueue& transferQueue)
{
	ASSERT(frameCtx.combinedGPUStaging.buffer != VK_NULL_HANDLE &&
		"[DrawPreparation::uploadGPUBuffersForFrame] combinedGPUstaging buffer is invalid.");

	const auto& globalAddrTable = gpuResources.getAddressTable();

	CombinedUploadPlan plan = stageCombinedUploads(frameCtx, globalAddrTable, transforms, gpuResources.getAllocator());

	// Record big transfer copies for indirect, instance, frame address table buffer, and global transforms/global address table
	CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {

		// visible instance data
		VkBufferCopy visInstCpy{};
		visInstCpy.srcOffset = plan.visOff;
		visInstCpy.dstOffset = 0;
		visInstCpy.size = plan.visSize;
		vkCmdCopyBuffer(cmd,
			frameCtx.combinedGPUStaging.buffer,
			frameCtx.visibleInstancesBuffer.buffer,
			1,
			&visInstCpy);

		// indirect draw commands
		VkBufferCopy indirectDrawsCpy{};
		indirectDrawsCpy.srcOffset = plan.indOff;
		indirectDrawsCpy.dstOffset = 0;
		indirectDrawsCpy.size = plan.indSize;
		vkCmdCopyBuffer(cmd,
			frameCtx.combinedGPUStaging.buffer,
			frameCtx.indirectDrawsBuffer.buffer,
			1,
			&indirectDrawsCpy);

		// GPU address table copy
		VkBufferCopy addressCpy{};
		addressCpy.srcOffset = plan.fAddrOff;
		addressCpy.dstOffset = 0;
		addressCpy.size = plan.fAddrSize;
		vkCmdCopyBuffer(cmd,
			frameCtx.combinedGPUStaging.buffer,
			frameCtx.addressTableBuffer.buffer,
			1,
			&addressCpy);
		// Always mark dirty with copy
		frameCtx.addressTableDirty = true;

		if (frameCtx.transformsBufferUploadNeeded) {
			const auto& globalAddrTableBuf = gpuResources.getAddressTableBuffer();
			const auto& transformsBuf = gpuResources.getGPUAddrsBuffer(AddressBufferType::Transforms).buffer;

			VkBufferCopy transformsCpy{};
			transformsCpy.srcOffset = plan.transformOff;
			transformsCpy.dstOffset = 0;
			transformsCpy.size = plan.transformSize;
			vkCmdCopyBuffer(cmd,
				frameCtx.combinedGPUStaging.buffer,
				transformsBuf,
				1,
				&transformsCpy);

			VkBufferCopy addressTableCpy{};
			addressTableCpy.srcOffset = plan.gAddrOff;
			addressTableCpy.dstOffset = 0;
			addressTableCpy.size = plan.gAddrSize;
			vkCmdCopyBuffer(cmd,
				frameCtx.combinedGPUStaging.buffer,
				globalAddrTableBuf.buffer,
				1,
				&addressTableCpy);

			BarrierUtils::releaseTransferToShaderReadQ(cmd, globalAddrTableBuf);
		}

		BarrierUtils::releaseTransferToShaderReadQ(cmd, frameCtx.addressTableBuffer);

	}, frameCtx.transferPool, QueueType::Transfer, Backend::getDevice());

	frameCtx.collectAndAppendCmds(std::move(DeferredCmdSubmitQueue::collectTransfer()), QueueType::Transfer);

	auto& transferSync = Renderer::_transferSync;
	const uint64_t signalValue = transferQueue.submitWithTimelineSync(
		frameCtx.transferCmds,
		transferSync.semaphore,
		++transferSync.signalValue
	);

	frameCtx.stashSubmitted(QueueType::Transfer);
	frameCtx.transferWaitValue = signalValue;
}

static glm::mat4 makeGridTransform(uint32_t index, uint32_t count, float spacing) {
	uint32_t gridSize = static_cast<uint32_t>(std::ceil(std::sqrt(count)));
	uint32_t x = index % gridSize;
	uint32_t z = index / gridSize;

	glm::vec3 translation = glm::vec3(x * spacing, 0.0f, z * spacing);
	return glm::translate(glm::mat4(1.0f), translation);
}

static glm::mat4 backAndForthX(float step = 0.03f, float minX = -2.0f, float maxX = 2.0f) {
	static float x = 0.0f;
	static float dir = 1.0f;
	x += dir * step;
	if (x >= maxX) { x = maxX; dir = -1.0f; }
	if (x <= minX) { x = minX; dir = 1.0f; }
	return glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, 0.0f));
}

void DrawPreparation::syncGlobalInstancesAndTransforms(
	FrameContext& frameCtx,
	GPUResources& gpuResources,
	std::unordered_map<SceneID, SceneProfileEntry>& sceneProfiles,
	std::vector<GlobalInstance>& globalInstances,
	std::vector<glm::mat4>& globalTransforms)
{
	bool anyTransformChanged = false;

	for (auto& inst : globalInstances) {
		SceneID sid = static_cast<SceneID>(inst.sceneID);
		SceneProfileEntry& profile = sceneProfiles.at(sid);

		if (profile.instanceCount == 1) {
			if (profile.drawType == DrawType::DrawStatic) {
				inst.drawType = profile.drawType;
				continue; // transforms already baked into static
			}
			if (profile.drawType == DrawType::DrawDynamic) {
				inst.drawType = profile.drawType;
				glm::mat4& M = globalTransforms[inst.firstTransform];
				M = glm::rotate(glm::mat4(1.0f), 0.005f, glm::vec3(0.0f, 1.0f, 0.0f)) * M;
				//M = backAndForthX(0.03f, -1.5f, 1.5f) * M;

				anyTransformChanged = true;
				continue;
			}
		}

		// Defined from copy values append list or decrease list
		// on first run this will always be an append
		//if (profile.drawType == DrawType::DrawMultiStatic || profile.instanceCount > 1) {
		//	// If instance count didn't change, skip
		//	if (inst.capacityCopies + 1 == profile.instanceCount) {
		//		continue;
		//	}

		//	inst.drawType = DrawType::DrawMultiStatic;

		//	glm::mat4 baseTransform = globalTransforms[inst.firstTransform];
		//	uint32_t currentCopies = inst.capacityCopies;
		//	uint32_t neededCopies = profile.instanceCount;

		//	fmt::print("[syncGI] multistatic: currentCopies={} neededCopies={} baseT={} staticTfSize(before)={}\n",
		//		currentCopies, neededCopies, inst.firstTransform, globalTransforms.size());

		//	if (neededCopies > currentCopies) {
		//		// Append new transforms
		//		for (uint32_t i = currentCopies; i < neededCopies; ++i) {
		//			glm::mat4 offset = makeGridTransform(i, neededCopies, 2.0f);
		//			globalTransforms.push_back(offset * baseTransform);
		//		}
		//		fmt::print("[syncGI] appended {} transforms, staticTfSize(after)={}\n",
		//			neededCopies - currentCopies, globalTransforms.size());
		//	}
		//	inst.capacityCopies = neededCopies;
		//	inst.transformCount = profile.instanceCount;

		//	frameCtx.transformsBufferUploadNeeded = true;
		//}
	}

	if (anyTransformChanged) {
		frameCtx.transformsBufferUploadNeeded = true;
	}

	// First time creation on frame 0
	if (!gpuResources.containsGPUBuffer(AddressBufferType::Transforms)) {
		fmt::print("[syncGobalInstances] create Transforms GPU buffer\n");

		auto& globalAddrsTable = gpuResources.getAddressTable();
		const auto allocator = gpuResources.getAllocator();

		const size_t transformsBytes = globalTransforms.size() * sizeof(glm::mat4);

		AllocatedBuffer newTransformBuf = BufferUtils::createGPUAddressBuffer(
			AddressBufferType::Transforms,
			globalAddrsTable,
			transformsBytes,
			allocator);
		// Note: The current global address table gets marked dirty, only the internal upload function marks it back to false.
		gpuResources.addGPUBufferToGlobalAddress(AddressBufferType::Transforms, newTransformBuf);

		fmt::print("[syncGobalInstances] new buffer=0x{:x} size={}\n", (uint64_t)newTransformBuf.buffer, newTransformBuf.info.size);

		frameCtx.transformsBufferUploadNeeded = true;
	}
}
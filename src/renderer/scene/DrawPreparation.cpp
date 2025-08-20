#include "pch.h"

#include "DrawPreparation.h"
#include "engine/Engine.h"
#include "utils/BufferUtils.h"

// TODO: Adjust this more, worldaabbs and transformsIDs are bad

// All render data is reset prior to this each frame
void DrawPreparation::buildAndSortIndirectDraws(
	FrameContext& frameCtx,
	const std::vector<GPUMeshData>& meshes,
	const std::vector<AABB>& worldAABBs,
	const glm::vec4 cameraPos)
{
	// Partition visible instances, while remembering their original indices
	std::vector<GPUInstance> opaqueInstances;
	std::vector<GPUInstance> transparentInstances;
	std::vector<uint32_t> opaqueVisIdx;
	std::vector<uint32_t> transparentVisIdx;

	opaqueInstances.reserve(frameCtx.visibleInstances.size());
	transparentInstances.reserve(frameCtx.visibleInstances.size());
	opaqueVisIdx.reserve(frameCtx.visibleInstances.size());
	transparentVisIdx.reserve(frameCtx.visibleInstances.size());

	for (uint32_t i = 0; i < frameCtx.visibleInstances.size(); ++i) {
		const auto& inst = frameCtx.visibleInstances[i];
		if (static_cast<MaterialPass>(inst.passType) == MaterialPass::Opaque) {
			opaqueInstances.push_back(inst);
			opaqueVisIdx.push_back(i);           // <-- keep index into worldAABBs
		}
		else {
			transparentInstances.push_back(inst);
			transparentVisIdx.push_back(i);      // <-- keep index into worldAABBs
		}
	}

	// === BATCH OPAQUE INSTANCES ===
	std::unordered_map<OpaqueBatchKey, std::vector<uint32_t>, OpaqueBatchKeyHash> opaqueBatches;
	for (uint32_t i = 0; i < opaqueInstances.size(); ++i) {
		const GPUInstance& inst = opaqueInstances[i];
		const OpaqueBatchKey key{ inst.meshID, inst.materialID };
		opaqueBatches[key].push_back(i);
	}

	frameCtx.indirectDraws.reserve(opaqueBatches.size() + transparentInstances.size());

	// Rebuild instances in draw order
	frameCtx.visibleInstances.clear();
	frameCtx.visibleInstances.reserve(opaqueInstances.size() + transparentInstances.size());

	// Opaque first
	frameCtx.opaqueRange.first = 0;
	for (const auto& [key, instanceIndices] : opaqueBatches) {
		const GPUMeshData& mesh = meshes[key.meshID];

		ASSERT(mesh.firstIndex + mesh.indexCount <= frameCtx.drawDataPC.totalIndexCount &&
			"[DrawPrep] Opaque draws would read past end of index buffer.");
		ASSERT(mesh.vertexOffset + mesh.vertexCount <= frameCtx.drawDataPC.totalVertexCount &&
			"[DrawPrep] Opaque draws would read past end of vertex buffer.");

		VkDrawIndexedIndirectCommand cmd{
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

	// TODO: get transparent sorting working, accessing worldaabbs directly isn't possible yet
	// === SORT AND BUILD TRANSPARENT ===
	if (!transparentInstances.empty()) {
		frameCtx.transparentRange.first = frameCtx.opaqueRange.visibleCount;
		frameCtx.transparentRange.visibleCount = static_cast<uint32_t>(transparentInstances.size());

		// Sort using AABBs aligned with the original visible list
		std::vector<uint32_t> order(transparentInstances.size());
		std::iota(order.begin(), order.end(), 0);

		const glm::vec3 camPos = glm::vec3(cameraPos);
		std::sort(order.begin(), order.end(), [&](uint32_t ia, uint32_t ib) {
			const auto& aabbA = worldAABBs[transparentVisIdx[ia]];
			const auto& aabbB = worldAABBs[transparentVisIdx[ib]];
			return glm::length(aabbA.origin - camPos) > glm::length(aabbB.origin - camPos);
		});

		for (uint32_t i = 0; i < order.size(); ++i) {
			const GPUInstance& inst = transparentInstances[order[i]];
			const GPUMeshData& mesh = meshes[inst.meshID];

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
			frameCtx.visibleInstances.push_back(inst);
		}
	}
}


void DrawPreparation::uploadGPUBuffersForFrame(FrameContext& frameCtx, GPUQueue& transferQueue) {
	ASSERT(frameCtx.combinedGPUStaging.buffer != VK_NULL_HANDLE &&
		"[DrawPreparation::uploadGPUBuffersForFrame] combinedGPUstaging buffer is invalid.");

	size_t visInstBytes = frameCtx.visibleInstances.size() * sizeof(GPUInstance);
	size_t indirectDrawBytes = frameCtx.indirectDraws.size() * sizeof(VkDrawIndexedIndirectCommand);

	uint8_t* mappedStagingPtr = static_cast<uint8_t*>(frameCtx.combinedGPUStaging.info.pMappedData);

	const size_t visInstOffset = 0;
	const size_t indirectDrawOffset = visInstOffset + visInstBytes;
	const size_t addrsTableOffset = indirectDrawOffset + indirectDrawBytes;

	// visible instances buffer staging
	memcpy(mappedStagingPtr, frameCtx.visibleInstances.data(), visInstBytes);

	// indirect draws buffer staging
	memcpy(mappedStagingPtr + indirectDrawOffset, frameCtx.indirectDraws.data(), indirectDrawBytes);

	// frame address table staging
	memcpy(mappedStagingPtr + addrsTableOffset, &frameCtx.addressTable, frameCtx.addressTableBuffer.info.size);

	// Record big transfer copies for indirect, instance, and main frame address table buffers
	CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {

		// visible instance data
		VkBufferCopy visInstCpy{};
		visInstCpy.srcOffset = visInstOffset;
		visInstCpy.dstOffset = 0;
		visInstCpy.size = visInstBytes;
		vkCmdCopyBuffer(cmd,
			frameCtx.combinedGPUStaging.buffer,
			frameCtx.visibleInstancesBuffer.buffer,
			1,
			&visInstCpy);

		// indirect draw commands
		VkBufferCopy indirectDrawsCpy{};
		indirectDrawsCpy.srcOffset = indirectDrawOffset;
		indirectDrawsCpy.dstOffset = 0;
		indirectDrawsCpy.size = indirectDrawBytes;
		vkCmdCopyBuffer(cmd,
			frameCtx.combinedGPUStaging.buffer,
			frameCtx.indirectDrawsBuffer.buffer,
			1,
			&indirectDrawsCpy);

		// GPU address table copy
		VkBufferCopy addressCpy{};
		addressCpy.srcOffset = addrsTableOffset;
		addressCpy.dstOffset = 0;
		addressCpy.size = frameCtx.addressTableBuffer.info.size;
		vkCmdCopyBuffer(cmd,
			frameCtx.combinedGPUStaging.buffer,
			frameCtx.addressTableBuffer.buffer,
			1,
			&addressCpy);

		frameCtx.addressTableDirty = true;

		BarrierUtils::releaseTransferToShaderReadQ(cmd, frameCtx.addressTableBuffer);

	}, frameCtx.transferPool, QueueType::Transfer, Backend::getDevice());

	frameCtx.collectAndAppendCmds(std::move(DeferredCmdSubmitQueue::collectTransfer()), QueueType::Transfer);

	auto& transferSync = Renderer::_transferSync;
	if (transferQueue.wasUsed) {
		transferQueue.submitWithTimelineSync(
			frameCtx.transferCmds,
			transferSync.semaphore,
			transferSync.signalValue,
			transferSync.semaphore,
			frameCtx.transferWaitValue
		);
		transferQueue.wasUsed = false;
	}
	else {
		transferQueue.submitWithTimelineSync(
			frameCtx.transferCmds,
			transferSync.semaphore,
			transferSync.signalValue
		);
	}

	frameCtx.stashSubmitted(QueueType::Transfer);

	frameCtx.transferWaitValue = transferSync.signalValue++;
}

static glm::mat4 makeGridTransform(uint32_t index, uint32_t count, float spacing) {
	uint32_t gridSize = static_cast<uint32_t>(std::ceil(std::sqrt(count)));
	uint32_t x = index % gridSize;
	uint32_t z = index / gridSize;

	glm::vec3 translation = glm::vec3(x * spacing, 0.0f, z * spacing);
	return glm::translate(glm::mat4(1.0f), translation);
}

// Note: DrawStatic brings no errors but static multidraw type brings queue errors
void DrawPreparation::syncGlobalInstancesAndTransforms(
	FrameContext& frameCtx,
	GPUResources& gpuResources,
	std::unordered_map<SceneID, SceneProfileEntry>& sceneProfiles,
	std::vector<GlobalInstance>& globalInstances,
	std::vector<glm::mat4>& globalTransforms,
	GPUQueue& transferQueue)
{
	bool firstTimeUpload = false;
	if (!gpuResources.containsGPUBuffer(AddressBufferType::Transforms)) {
		firstTimeUpload = true; // first time creation
		frameCtx.staticTransformsUploadNeeded = true; // enables the barrier on first frame graphics recording
	}

	for (auto& inst : globalInstances) {
		SceneID sid = static_cast<SceneID>(inst.sceneID);
		SceneProfileEntry& profile = sceneProfiles.at(sid);

		if (profile.drawType == DrawType::DrawStatic || profile.instanceCount < 2) {
			inst.drawType = DrawType::DrawStatic;
			inst.transformCount = 1;
			continue; // transforms already baked into static
		}

		// Defined from copy values append list or decrease list
		// on first run this will always be an append
		if (profile.drawType == DrawType::DrawMultiStatic || profile.instanceCount > 1) {
			// If instance count didn't change, skip
			if (inst.capacityCopies + 1 == profile.instanceCount) {
				continue;
			}

			inst.drawType = DrawType::DrawMultiStatic;

			glm::mat4 baseTransform = globalTransforms[inst.firstTransform];
			uint32_t currentCopies = inst.capacityCopies;
			uint32_t neededCopies = profile.instanceCount;

			fmt::print("[syncGI] multistatic: currentCopies={} neededCopies={} baseT={} staticTfSize(before)={}\n",
				currentCopies, neededCopies, inst.firstTransform, globalTransforms.size());

			if (neededCopies > currentCopies) {
				// Append new transforms
				for (uint32_t i = currentCopies; i < neededCopies; ++i) {
					glm::mat4 offset = makeGridTransform(i, neededCopies, 2.0f);
					globalTransforms.push_back(offset * baseTransform);
				}
				fmt::print("[syncGI] appended {} transforms, staticTfSize(after)={}\n",
					neededCopies - currentCopies, globalTransforms.size());
			}
			inst.capacityCopies = neededCopies;
			inst.transformCount = profile.instanceCount;

			frameCtx.staticTransformsUploadNeeded = true;
		}
	}

	if (!frameCtx.staticTransformsUploadNeeded || !firstTimeUpload) return;

	auto& globalAddrsTableBuf = gpuResources.getAddressTableBuffer();
	auto& globalAddrsTable = gpuResources.getAddressTable();

	size_t transformsBytes = globalTransforms.size() * sizeof(glm::mat4);
	fmt::print("[syncGI] sizes: xformsBytes={} addrTableBytes={}\n",
		transformsBytes, globalAddrsTableBuf.info.size);

	// Need to create buffer for first time
	if (firstTimeUpload) {
		fmt::print("[syncGI] create Transforms GPU buffer\n");
		AllocatedBuffer newTransformBuf = BufferUtils::createGPUAddressBuffer(
			AddressBufferType::Transforms,
			globalAddrsTable,
			transformsBytes,
			gpuResources.getAllocator());
		// Note: The current global address table gets marked dirty, only the internal upload function marks it back to false.
		gpuResources.addGPUBufferToGlobalAddress(AddressBufferType::Transforms, newTransformBuf);
		fmt::print("[syncGI] new buffer=0x{:x} size={}\n",
			(uint64_t)newTransformBuf.buffer, newTransformBuf.info.size);
	}
	auto& staticTransformsBuf = gpuResources.getGPUAddrsBuffer(AddressBufferType::Transforms);

	uint8_t* mappedStagingPtr = static_cast<uint8_t*>(frameCtx.combinedGPUStaging.info.pMappedData);

	const size_t transformOffset = 0;
	const size_t addrsTableOffset = transformOffset + globalAddrsTableBuf.info.size;

	memcpy(mappedStagingPtr, globalTransforms.data(), transformsBytes);

	memcpy(mappedStagingPtr + addrsTableOffset, &globalAddrsTable, globalAddrsTableBuf.info.size);

	const auto device = Backend::getDevice();

	// Upload static transforms and update global address table
	CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {
		// indirect draw commands
		VkBufferCopy staticTransformCpy{};
		staticTransformCpy.srcOffset = transformOffset;
		staticTransformCpy.dstOffset = 0;
		staticTransformCpy.size = transformsBytes;

		vkCmdCopyBuffer(cmd,
			frameCtx.combinedGPUStaging.buffer,
			staticTransformsBuf.buffer,
			1,
			&staticTransformCpy);

		VkBufferCopy addressTableCpy{};
		addressTableCpy.srcOffset = addrsTableOffset;
		addressTableCpy.dstOffset = 0;
		addressTableCpy.size = globalAddrsTableBuf.info.size;

		vkCmdCopyBuffer(cmd,
			frameCtx.combinedGPUStaging.buffer,
			globalAddrsTableBuf.buffer,
			1,
			&addressTableCpy);

		BarrierUtils::releaseTransferToShaderReadQ(cmd, globalAddrsTableBuf);
		fmt::print("[syncGI] release -> shaderRead on addrTable\n");

	}, frameCtx.transferPool, QueueType::Transfer, device);

	frameCtx.collectAndAppendCmds(std::move(DeferredCmdSubmitQueue::collectTransfer()), QueueType::Transfer);
	fmt::print("[syncGI] collected transfer cmdBufs -> {}\n", frameCtx.transferCmds.size());

	auto& transferSync = Renderer::_transferSync;
	transferQueue.submitWithTimelineSync(
		frameCtx.transferCmds,
		transferSync.semaphore,
		transferSync.signalValue,
		nullptr,
		0,
		true // transfer queue waits ahead
	);
	fmt::print("[syncGI] submit: signalValue={} waitValue={} count={}\n",
		transferSync.signalValue, 0u, frameCtx.transferCmds.size());

	frameCtx.stashSubmitted(QueueType::Transfer);
	fmt::print("[syncGI] stashed submitted transfers for freeing later\n");

	frameCtx.transferWaitValue = transferSync.signalValue++;
	fmt::print("[syncGI] next signalValue={} (frameCtx.transferWaitValue={})\n",
		transferSync.signalValue, frameCtx.transferWaitValue);

	// Update the global set
	frameCtx.descriptorWriter.clear();
	const auto unifiedSet = DescriptorSetOverwatch::getUnifiedDescriptors().descriptorSet;
	frameCtx.descriptorWriter.writeBuffer(
		ADDRESS_TABLE_BINDING,
		globalAddrsTableBuf.buffer,
		globalAddrsTableBuf.info.size,
		0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		unifiedSet);

	frameCtx.descriptorWriter.updateSet(device, unifiedSet);
}
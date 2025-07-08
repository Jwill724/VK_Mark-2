#include "pch.h"

#include "DrawPreperation.h"
#include "Engine.h"
#include "RenderScene.h"
#include "gpu_types/CommandBuffer.h"
#include "utils/BufferUtils.h"

void DrawPreperation::buildAndSortIndirectDraws(
	FrameContext& frameCtx,
	const std::vector<GPUDrawRange>& drawRanges,
	const std::vector<GPUMeshData>& meshes)
{
	// === BATCH OPAQUE INSTANCES ===
	std::unordered_map<OpaqueBatchKey, std::vector<uint32_t>, OpaqueBatchKeyHash> opaqueBatches;

	for (uint32_t i = 0; i < frameCtx.opaqueInstances.size(); ++i) {
		const GPUInstance& inst = frameCtx.opaqueInstances[i];
		const OpaqueBatchKey key{ inst.meshID, inst.materialID };
		opaqueBatches[key].push_back(i);
	}

	frameCtx.opaqueIndirectDraws.reserve(opaqueBatches.size());

	uint32_t batchIndex = 0;
	for (const auto& [key, instanceIndices] : opaqueBatches) {
		const GPUMeshData& mesh = meshes[key.meshID];

		if (mesh.drawRangeID >= drawRanges.size()) {
			fmt::print("WARNING: [Opaque] Skipped batch {} - meshID={} drawRangeID={} (out of bounds)\n",
				batchIndex, key.meshID, mesh.drawRangeID);
			continue;
		}

		const GPUDrawRange& range = drawRanges[mesh.drawRangeID];

		fmt::print("[Opaque Batch {}] meshID={} materialID={} drawRangeID={} -> indexCount={}, vertexOffset={}, firstIndex={}, vertexCount={},instanceCount={}\n",
			batchIndex, key.meshID, key.materialID, mesh.drawRangeID,
			range.indexCount, range.vertexOffset, range.firstIndex, range.vertexCount, instanceIndices.size());

		VkDrawIndexedIndirectCommand cmd = {
			.indexCount = range.indexCount,
			.instanceCount = static_cast<uint32_t>(instanceIndices.size()),
			.firstIndex = range.firstIndex,
			.vertexOffset = static_cast<int32_t>(range.vertexOffset),
			.firstInstance = frameCtx.opaqueVisibleCount
		};

		fmt::print("  -> Cmd: idxCount={}, instCount={}, firstIdx={}, vertOff={}, firstInst={}\n",
			cmd.indexCount, cmd.instanceCount, cmd.firstIndex, cmd.vertexOffset, cmd.firstInstance);

		frameCtx.opaqueIndirectDraws.emplace_back(cmd);
		frameCtx.opaqueVisibleCount += cmd.instanceCount;
		++batchIndex;
	}

	// === SORT & DRAW TRANSPARENT ===
	if (!frameCtx.transparentInstances.empty()) {
		frameCtx.transparentIndirectDraws.reserve(frameCtx.transparentInstances.size());
		frameCtx.transparentVisibleCount = static_cast<uint32_t>(frameCtx.transparentInstances.size());

		glm::vec3 camPos = glm::vec3(RenderScene::getCurrentSceneData().cameraPosition);

		std::sort(frameCtx.transparentInstances.begin(), frameCtx.transparentInstances.end(),
			[&](const GPUInstance& A, const GPUInstance& B) {
				const auto& aabbA = meshes[A.meshID].worldAABB;
				const auto& aabbB = meshes[B.meshID].worldAABB;
				return glm::length(aabbA.origin - camPos) > glm::length(aabbB.origin - camPos);
			});

		for (uint32_t i = 0; i < frameCtx.transparentInstances.size(); ++i) {
			const GPUInstance& inst = frameCtx.transparentInstances[i];
			const GPUMeshData& mesh = meshes[inst.meshID];

			if (mesh.drawRangeID >= drawRanges.size()) {
				fmt::print("WARNING: [Transparent] Skipped instance {} - meshID={} drawRangeID={} (out of bounds)\n",
					i, inst.meshID, mesh.drawRangeID);
				continue;
			}

			const auto& range = drawRanges[mesh.drawRangeID];

			fmt::print("[Transparent Instance {}] meshID={} -> drawRangeID={} -> indexCount={}, vertexOffset={}, firstIndex={}\n",
				i, inst.meshID, mesh.drawRangeID, range.indexCount, range.vertexOffset, range.firstIndex);

			VkDrawIndexedIndirectCommand cmd = {
				.indexCount = range.indexCount,
				.instanceCount = 1,
				.firstIndex = range.firstIndex,
				.vertexOffset = static_cast<int32_t>(range.vertexOffset),
				.firstInstance = i
			};

			fmt::print("  -> Cmd: idxCount={}, instCount={}, firstIdx={}, vertOff={}, firstInst={}\n",
				cmd.indexCount, cmd.instanceCount, cmd.firstIndex, cmd.vertexOffset, cmd.firstInstance);

			frameCtx.transparentIndirectDraws.emplace_back(cmd);
		}
	}

	fmt::print("[DrawPrep] Opaque batches: {}, total instances: {}\n",
		frameCtx.opaqueIndirectDraws.size(), frameCtx.opaqueVisibleCount);
	fmt::print("[DrawPrep] Transparent instances: {}\n", frameCtx.transparentVisibleCount);
}


void DrawPreperation::uploadGPUBuffersForFrame(FrameContext& frameCtx, GPUQueue& transferQueue, const VmaAllocator allocator) {
	ASSERT(frameCtx.combinedGPUStaging.buffer != VK_NULL_HANDLE &&
		"[DrawPreperation::uploadGPUBuffersForFrame] combinedGPUstaging buffer is invalid.");

	ASSERT(frameCtx.transformsList.size() > 0 &&
		"[DrawPreperation::uploadGPUBuffersForFrame] transformList is empty, require valid transforms.");

	bool isOpaqueVisible = false;
	bool isTransparentVisible = false;

	if (frameCtx.opaqueVisibleCount > 0)
		isOpaqueVisible = true;
	if (frameCtx.transparentVisibleCount > 0)
		isTransparentVisible = true;

	size_t opaqueInstanceBytes = 0;
	size_t opaqueIndirectBytes = 0;
	size_t transparentInstanceBytes = 0;
	size_t transparentIndirectBytes = 0;

	size_t transformsListBytes = frameCtx.transformsList.size() * sizeof(glm::mat4);

	if (isOpaqueVisible) {
		frameCtx.opaqueInstanceBuffer = BufferUtils::createGPUAddressBuffer(
			AddressBufferType::OpaqueIntances, frameCtx.addressTable, OPAQUE_INSTANCE_SIZE_BYTES, allocator);

		frameCtx.opaqueIndirectCmdBuffer = BufferUtils::createGPUAddressBuffer(
			AddressBufferType::OpaqueIndirectDraws, frameCtx.addressTable, OPAQUE_INDIRECT_SIZE_BYTES, allocator);


		opaqueInstanceBytes = frameCtx.opaqueInstances.size() * sizeof(GPUInstance);
		opaqueIndirectBytes = frameCtx.opaqueIndirectDraws.size() * sizeof(VkDrawIndexedIndirectCommand);
	}

	if (isTransparentVisible) {
		frameCtx.transparentInstanceBuffer = BufferUtils::createGPUAddressBuffer(
			AddressBufferType::TransparentInstances, frameCtx.addressTable, TRANSPARENT_INSTANCE_SIZE_BYTES, allocator);

		frameCtx.transparentIndirectCmdBuffer = BufferUtils::createGPUAddressBuffer(
			AddressBufferType::TransparentIndirectDraws, frameCtx.addressTable, TRANSPARENT_INDIRECT_SIZE_BYTES, allocator);

		transparentInstanceBytes = frameCtx.transparentInstances.size() * sizeof(GPUInstance);
		transparentIndirectBytes = frameCtx.transparentIndirectDraws.size() * sizeof(VkDrawIndexedIndirectCommand);
	}

	frameCtx.transformsListBuffer = BufferUtils::createGPUAddressBuffer(
		AddressBufferType::Transforms, frameCtx.addressTable, MAX_VISIBLE_TRANSFORMS, allocator);


	//fmt::print("\n[DRAW PREP]opaqueInstBytes: {}\n", opaqueInstanceBytes);
	//fmt::print("\n[DRAW PREP]opauqeIndirectBytes: {}\n", opaqueIndirectBytes);
	//fmt::print("\n[DRAW PREP]transparentInstBytes: {}\n", transparentInstanceBytes);
	//fmt::print("\n[DRAW PREP]transparentIndirectBytes: {}\n", transparentIndirectBytes);
	//fmt::print("\n[DRAW PREP]transformslistbytes: {}\n", transformsListBytes);

	uint8_t* mappedStagingPtr = static_cast<uint8_t*>(frameCtx.combinedGPUStaging.info.pMappedData);
	size_t offset = 0;

	// Must be in this exact order Opaque->Transparent in memory offsetting
	if (isOpaqueVisible) {
		memcpy(mappedStagingPtr, frameCtx.opaqueInstances.data(), opaqueInstanceBytes);
		offset += opaqueInstanceBytes;

		memcpy(mappedStagingPtr + offset, frameCtx.opaqueIndirectDraws.data(), opaqueIndirectBytes);
	}
	// Only advance offset if transparent section won't follow.
	// If transparents are not visible, opaque is the last upload,
	// so the offset needs a manual offset advance
	if (isTransparentVisible) {
		if (isOpaqueVisible) {
			offset += opaqueIndirectBytes;
		}

		memcpy(mappedStagingPtr + offset, frameCtx.transparentInstances.data(), transparentInstanceBytes);
		offset += transparentInstanceBytes;

		memcpy(mappedStagingPtr + offset, frameCtx.transparentIndirectDraws.data(), transparentIndirectBytes);
	}

	// transformslist staging
	if (isOpaqueVisible && !isTransparentVisible)
		offset += opaqueIndirectBytes;
	else
		offset += transparentIndirectBytes;

	memcpy(mappedStagingPtr + offset, frameCtx.transformsList.data(), transformsListBytes);

	// frame address table
	ASSERT(frameCtx.addressTableStaging.buffer != VK_NULL_HANDLE);
	memcpy(frameCtx.addressTableStaging.info.pMappedData, &frameCtx.addressTable, sizeof(GPUAddressTable));

	// Record big transfer copies for indirect, instance, and main frame address table buffers
	CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {
		size_t offset = 0;

		if (isOpaqueVisible) {
			// Opaque instance data
			VkBufferCopy opaqueInstCpy{};
			opaqueInstCpy.srcOffset = offset;
			opaqueInstCpy.dstOffset = 0;
			opaqueInstCpy.size = opaqueInstanceBytes;
			vkCmdCopyBuffer(cmd,
				frameCtx.combinedGPUStaging.buffer,
				frameCtx.opaqueInstanceBuffer.buffer,
				1,
				&opaqueInstCpy);
			offset += opaqueInstanceBytes;

			// Opaque indirect draw commands
			VkBufferCopy opaqueIndirectCpy{};
			opaqueIndirectCpy.srcOffset = offset;
			opaqueIndirectCpy.dstOffset = 0;
			opaqueIndirectCpy.size = opaqueIndirectBytes;
			vkCmdCopyBuffer(cmd,
				frameCtx.combinedGPUStaging.buffer,
				frameCtx.opaqueIndirectCmdBuffer.buffer,
				1,
				&opaqueIndirectCpy);
		}

		if (isTransparentVisible) {
			if (isOpaqueVisible) {
				offset += opaqueIndirectBytes;
			}

			// Transparent instances
			VkBufferCopy transparentInstCpy{};
			transparentInstCpy.srcOffset = offset;
			transparentInstCpy.dstOffset = 0;
			transparentInstCpy.size = transparentInstanceBytes;
			vkCmdCopyBuffer(cmd,
				frameCtx.combinedGPUStaging.buffer,
				frameCtx.transparentInstanceBuffer.buffer,
				1,
				&transparentInstCpy);
			offset += transparentInstanceBytes;

			// Transparent indirect draw commands
			VkBufferCopy transparentIndirectCpy{};
			transparentIndirectCpy.srcOffset = offset;
			transparentIndirectCpy.dstOffset = 0;
			transparentIndirectCpy.size = transparentIndirectBytes;
			vkCmdCopyBuffer(cmd,
				frameCtx.combinedGPUStaging.buffer,
				frameCtx.transparentIndirectCmdBuffer.buffer,
				1,
				&transparentIndirectCpy);
		}

		// Transform buffer copy
		if (isOpaqueVisible && !isTransparentVisible)
			offset += opaqueIndirectBytes;
		else
			offset += transparentIndirectBytes;

		VkBufferCopy transformListCpy{};
		transformListCpy.srcOffset = offset;
		transformListCpy.dstOffset = 0;
		transformListCpy.size = transformsListBytes;
		vkCmdCopyBuffer(cmd,
			frameCtx.combinedGPUStaging.buffer,
			frameCtx.transformsListBuffer.buffer,
			1,
			&transformListCpy);


		// GPU address table copy
		VkBufferCopy addressCpy{};
		addressCpy.size = sizeof(GPUAddressTable);
		vkCmdCopyBuffer(cmd, frameCtx.addressTableStaging.buffer, frameCtx.addressTableBuffer.buffer, 1, &addressCpy);
		frameCtx.addressTableDirty = true;

		if (GPU_ACCELERATION_ENABLED) {
			// GPU draw building next
			// compute must wait for transfer to complete
			VkMemoryBarrier2 memoryBarrier{
				.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
				.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
			};
			VkDependencyInfo dependencyInfo{
				.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
				.memoryBarrierCount = 1,
				.pMemoryBarriers = &memoryBarrier,
			};
			vkCmdPipelineBarrier2(cmd, &dependencyInfo);
		}

	}, frameCtx.transferPool, QueueType::Transfer);

	frameCtx.transferCmds = DeferredCmdSubmitQueue::collectTransfer();

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
		if (GPU_ACCELERATION_ENABLED) {
			transferQueue.submitWithTimelineSync(
				frameCtx.transferCmds,
				transferSync.semaphore,
				transferSync.signalValue,
				Renderer::_computeSync.semaphore,
				frameCtx.computeWaitValue
			);
		}
		else {
			transferQueue.submitWithTimelineSync(
				frameCtx.transferCmds,
				transferSync.semaphore,
				transferSync.signalValue
			);
		}
	}
	frameCtx.transferWaitValue = transferSync.signalValue++;

	std::vector<AllocatedBuffer> usedGPUBuffers;
	if (isOpaqueVisible) {
		auto opaqueInstBufCpy = frameCtx.opaqueInstanceBuffer;
		auto opaqueIndirectBufCpy = frameCtx.opaqueIndirectCmdBuffer;
		usedGPUBuffers.emplace_back(std::move(opaqueInstBufCpy));
		usedGPUBuffers.emplace_back(std::move(opaqueIndirectBufCpy));
	}
	if (isTransparentVisible) {
		auto transparentInstBufCpy = frameCtx.transparentInstanceBuffer;
		auto transparentIndirecBufCpy = frameCtx.transparentIndirectCmdBuffer;
		usedGPUBuffers.emplace_back(std::move(transparentInstBufCpy));
		usedGPUBuffers.emplace_back(std::move(transparentIndirecBufCpy));
	}
	auto transformListBufCpy = frameCtx.transformsListBuffer;
	usedGPUBuffers.emplace_back(std::move(transformListBufCpy));

	frameCtx.transferDeletion.enqueue(frameCtx.transferWaitValue, [usedGPUBuffers, allocator]() mutable {
		for (auto& buffer : usedGPUBuffers) {
			BufferUtils::destroyAllocatedBuffer(buffer, allocator);
		}
	});
}



void DrawPreperation::meshDataAndTransformsListUpload(
	FrameContext& frameCtx,
	MeshRegistry& meshes,
	const std::vector<glm::mat4>& transformsList,
	GPUQueue& transferQueue,
	const VmaAllocator allocator,
	bool uploadTransforms // in fully gpu driven this won't be an option, this all going to gpu only
) {
	ASSERT(!meshes.meshData.empty() && "[DrawPreperation] mesh data not set properly.");

	const uint32_t meshCount = static_cast<uint32_t>(meshes.meshData.size());
	const size_t meshIDBufSize = static_cast<size_t>(meshCount * sizeof(uint32_t));
	constexpr size_t visibleCountSize = sizeof(uint32_t);

	if (frameCtx.cullingPCData.meshCount != meshCount) {
		frameCtx.cullingPCData.meshCount = meshCount;
	}

	// I will have to expand on this check,
	// right now I only wanna update the buffer and the meshIDs when theres a change
	// currently its fully static mesh scenes so this will work
	bool meshIDsInitializied = false;
	if (meshes.meshIDBuffer.buffer == VK_NULL_HANDLE) {
		std::vector<MeshID> meshIDs = meshes.extractAllMeshIDs();
		if (!meshIDs.empty()) meshIDsInitializied = true;

		// === Mesh ID buffer: only allocate once unless dynamic scene ===
		meshes.meshIDBuffer = BufferUtils::createBuffer(
			meshIDBufSize,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VMA_MEMORY_USAGE_CPU_TO_GPU,
			allocator);


		ASSERT(meshes.meshIDBuffer.info.pMappedData != nullptr);
		memcpy(meshes.meshIDBuffer.mapped, meshIDs.data(), meshIDBufSize);

		frameCtx.cullingPCData.meshIDBufferAddr = meshes.meshIDBuffer.address;
	}

	if (meshIDsInitializied) {
		if (frameCtx.stagingVisibleMeshIDsBuffer.buffer == VK_NULL_HANDLE &&
			frameCtx.gpuVisibleMeshIDsBuffer.buffer == VK_NULL_HANDLE) {

			// After culling is performed a list of all visible meshIDs are returned
			frameCtx.stagingVisibleMeshIDsBuffer = BufferUtils::createBuffer(
				meshIDBufSize,
				VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
				VMA_MEMORY_USAGE_GPU_TO_CPU,
				allocator);

			ASSERT(frameCtx.stagingVisibleMeshIDsBuffer.info.pMappedData != nullptr);

			frameCtx.gpuVisibleMeshIDsBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::VisibleMeshIDs, frameCtx.addressTable, meshIDBufSize, allocator);

			frameCtx.cullingPCData.visibleCountOutBufferAddr = frameCtx.gpuVisibleCountBuffer.address;
		}
	}

	// visible count buffer is only created once in program lifetime
	if (!frameCtx.visibleCountInitialized) {
		if (frameCtx.stagingVisibleCountBuffer.buffer == VK_NULL_HANDLE &&
			frameCtx.gpuVisibleCountBuffer.buffer == VK_NULL_HANDLE) {
			// Atomically increments in compute shader to determine how many draw calls need to be made
			// Creates N amount of draw calls based off count
			frameCtx.stagingVisibleCountBuffer = BufferUtils::createBuffer(
				visibleCountSize,
				VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
				VMA_MEMORY_USAGE_GPU_TO_CPU,
				allocator);

			ASSERT(frameCtx.stagingVisibleCountBuffer.info.pMappedData != nullptr);

			frameCtx.gpuVisibleCountBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::VisibleCount, frameCtx.addressTable, visibleCountSize, allocator);

			frameCtx.cullingPCData.visibleCountOutBufferAddr = frameCtx.gpuVisibleCountBuffer.address;

			frameCtx.visibleCount = 0;
		}
	}

	const size_t transformsListBufferSize = meshCount * sizeof(glm::mat4);
	if (uploadTransforms) {
		// Will transform all aabbs in shader
		frameCtx.cullingPCData.rebuildTransforms = 1; // is set back to 0 after culling

		ASSERT(!transformsList.empty() && "[DrawPreperation] Transform list is empty.");

		if (frameCtx.transformsListBuffer.buffer == VK_NULL_HANDLE) {
			frameCtx.transformsListBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::Transforms,
				frameCtx.addressTable,
				transformsListBufferSize,
				allocator);
		}
	}


	size_t gpuStagingSize = 0;
	// visible mesh ids
	if (meshIDsInitializied)
		gpuStagingSize += meshIDBufSize;

	// One time setup for visibleCount
	if (!frameCtx.visibleCountInitialized)
		gpuStagingSize += visibleCountSize;

	if (uploadTransforms)
		gpuStagingSize += transformsListBufferSize;


	AllocatedBuffer gpuStaging = BufferUtils::createBuffer(
		gpuStagingSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
		allocator);
	ASSERT(gpuStaging.info.pMappedData);

	uint8_t* mappedStagingPtr = static_cast<uint8_t*>(gpuStaging.info.pMappedData);
	size_t offset = 0;

	if (uploadTransforms) {
		memcpy(mappedStagingPtr + offset, transformsList.data(), transformsListBufferSize);
		offset += transformsListBufferSize;
	}

	ASSERT(offset <= gpuStagingSize);

	ASSERT(frameCtx.addressTableStaging.buffer != VK_NULL_HANDLE);
	memcpy(frameCtx.addressTableStaging.info.pMappedData, &frameCtx.addressTable, sizeof(GPUAddressTable));

	CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {
		size_t offset = 0;

		if (meshIDsInitializied) {
			VkBufferCopy visibleMeshIDsCpy{};
			visibleMeshIDsCpy.srcOffset = offset;
			visibleMeshIDsCpy.dstOffset = 0;
			visibleMeshIDsCpy.size = meshIDBufSize;
			vkCmdCopyBuffer(
				cmd,
				gpuStaging.buffer,
				frameCtx.gpuVisibleMeshIDsBuffer.buffer,
				1,
				&visibleMeshIDsCpy);

			offset += meshIDBufSize;
		}

		if (!frameCtx.visibleCountInitialized) {
			VkBufferCopy visbleCountCpy{};
			visbleCountCpy.srcOffset = offset;
			visbleCountCpy.dstOffset = 0;
			visbleCountCpy.size = visibleCountSize;
			vkCmdCopyBuffer(
				cmd,
				gpuStaging.buffer,
				frameCtx.gpuVisibleCountBuffer.buffer,
				1,
				&visbleCountCpy);

			offset += visibleCountSize;

			// end of the line, set this to true once and forget about it
			frameCtx.visibleCountInitialized = true;
		}

		if (uploadTransforms) {
			VkBufferCopy transformsListCpy{};
			transformsListCpy.srcOffset = offset;
			transformsListCpy.dstOffset = 0;
			transformsListCpy.size = transformsListBufferSize;
			vkCmdCopyBuffer(cmd, gpuStaging.buffer, frameCtx.transformsListBuffer.buffer, 1, &transformsListCpy);
		}

		VkBufferCopy addressCpy{};
		addressCpy.size = sizeof(GPUAddressTable);
		vkCmdCopyBuffer(cmd, frameCtx.addressTableStaging.buffer, frameCtx.addressTableBuffer.buffer, 1, &addressCpy);
		frameCtx.addressTableDirty = true;

		}, frameCtx.transferPool, QueueType::Transfer);

	frameCtx.transferCmds = DeferredCmdSubmitQueue::collectTransfer();

	auto& sync = Renderer::_transferSync;
	transferQueue.submitWithTimelineSync(
		frameCtx.transferCmds,
		sync.semaphore,
		sync.signalValue,
		VK_NULL_HANDLE,
		0,
		true);
	frameCtx.transferWaitValue = sync.signalValue++;

	frameCtx.cpuDeletion.push_function([&gpuStaging, allocator]() mutable {
		BufferUtils::destroyAllocatedBuffer(gpuStaging, allocator);
	});
}
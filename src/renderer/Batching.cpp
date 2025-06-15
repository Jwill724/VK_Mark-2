#include "pch.h"

#include "Batching.h"
#include "Engine.h"
#include "RenderScene.h"
#include "gpu/CommandBuffer.h"
#include "utils/BufferUtils.h"

void Batching::buildInstanceBuffer(const std::vector<RenderObject>& objects, FrameContext& frame, GPUResources& resources) {
	frame.instanceData.clear();
	frame.instanceData.reserve(objects.size());

	auto& vertexBuf = resources.getVertexBuffer();
	auto& indexBuf = resources.getIndexBuffer();

	for (size_t i = 0; i < objects.size(); ++i) {
		const auto& obj = objects[i];

		assert(obj.instanceIndex < objects.size() && "instanceIndex out of bounds");
		assert(obj.drawRangeIndex < resources.getDrawRanges().size() && "drawRangeIndex out of bounds");
		assert(obj.materialIndex < resources.materialCount && "materialIndex out of bounds");

		InstanceData instance{};
		instance.modelMatrix = obj.modelMatrix;
		instance.localAABB = obj.aabb;
		instance.drawRangeIndex = obj.drawRangeIndex;
		instance.materialIndex = obj.materialIndex;
		instance.vertexBufferAddress = vertexBuf.address;
		instance.indexBufferAddress = indexBuf.address;

		fmt::print("[Instance {}] drawRangeIndex={}, materialIndex={}, vtxAddr=0x{:X}, idxAddr=0x{:X}\n",
			i, instance.drawRangeIndex, instance.materialIndex, instance.vertexBufferAddress, instance.indexBufferAddress);

		frame.instanceData.push_back(instance);
	}
}


void Batching::createIndirectCommandBuffer(
	const std::vector<RenderObject>& objects,
	FrameContext& frame,
	GPUResources& resources)
{
	frame.indirectDraws.clear();
	frame.indirectDraws.reserve(objects.size());

	fmt::print("Creating indirect draw command buffer...\n");
	fmt::print("RenderObject count: {}\n", objects.size());

	auto& idxBuffer = resources.getIndexBuffer();

	const auto& drawRanges = resources.getDrawRanges();

	for (size_t i = 0; i < objects.size(); ++i) {
		const auto& obj = objects[i];
		const auto& range = drawRanges[obj.drawRangeIndex];

		assert(obj.drawRangeIndex < drawRanges.size());
		assert((range.firstIndex + range.indexCount) * sizeof(uint32_t) <= idxBuffer.info.size);

		VkDrawIndexedIndirectCommand cmd{};
		cmd.indexCount = range.indexCount;
		cmd.instanceCount = 1;
		cmd.firstIndex = range.firstIndex;
		cmd.vertexOffset = range.vertexOffset;
		cmd.firstInstance = obj.instanceIndex;

		fmt::print("[DrawCmd {}] indexCount={}, firstIndex={}, vertexOffset={}, firstInstance={}\n",
			i, cmd.indexCount, cmd.firstIndex, cmd.vertexOffset, cmd.firstInstance);

		frame.indirectDraws.push_back(cmd);
	}
}

void Batching::uploadBuffersForFrame(FrameContext& frame, GPUResources& resources, GPUQueue& transferQueue) {
	if (frame.instanceData.empty() && frame.indirectDraws.empty()) {
		fmt::print("[Batching] Skipped upload: No instance or indirect draw data for this frame.\n");
		return;
	}

	for (const auto& cmd : frame.indirectDraws) {
		assert(cmd.firstInstance + cmd.instanceCount <= frame.instanceData.size() && "instance count larger than instanceData size");
	}

	auto allocator = resources.getAllocator();

	const size_t indirectBufferSize = frame.indirectDraws.size() * sizeof(VkDrawIndexedIndirectCommand);
	const size_t instBufferSize = frame.instanceData.size() * sizeof(InstanceData);

	// create gpu buffers
	frame.instanceBuffer = BufferUtils::createGPUAddressBuffer(
		AddressBufferType::Instance,
		frame.addressTable,
		instBufferSize,
		allocator
	);
	assert(frame.instanceBuffer.buffer != VK_NULL_HANDLE);

	frame.indirectCmdBuffer = BufferUtils::createGPUAddressBuffer(
		AddressBufferType::IndirectCmd,
		frame.addressTable,
		indirectBufferSize,
		allocator
	);
	assert(frame.indirectCmdBuffer.buffer != VK_NULL_HANDLE);

	// create cpu staging buffers
	AllocatedBuffer instStaging = BufferUtils::createBuffer(
		instBufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VMA_MEMORY_USAGE_CPU_ONLY,
		allocator);
	assert(instStaging.buffer != nullptr);
	std::memcpy(instStaging.info.pMappedData, frame.instanceData.data(), instBufferSize);

	AllocatedBuffer indirectStaging = BufferUtils::createBuffer(
		indirectBufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VMA_MEMORY_USAGE_CPU_ONLY,
		allocator
	);
	assert(indirectStaging.buffer != nullptr);
	std::memcpy(indirectStaging.info.pMappedData, frame.indirectDraws.data(), indirectBufferSize);

	// Frame gpu address creation and staging
	// Copy latest address data into mapped buffer
	assert(frame.addressTableStaging.buffer != VK_NULL_HANDLE);
	std::memcpy(frame.addressTableStaging.info.pMappedData, &frame.addressTable, sizeof(GPUAddressTable));

	// Record big transfer copies for both indirect, instance, and main frame address table buffers
	CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {
		VkBufferCopy instCpy{};
		instCpy.size = instBufferSize;
		vkCmdCopyBuffer(cmd, instStaging.buffer, frame.instanceBuffer.buffer, 1, &instCpy);

		VkBufferCopy indirectCpy{};
		indirectCpy.size = indirectBufferSize;
		vkCmdCopyBuffer(cmd, indirectStaging.buffer, frame.indirectCmdBuffer.buffer, 1, &indirectCpy);

		VkBufferCopy addressCpy{};
		addressCpy.size = sizeof(GPUAddressTable);
		vkCmdCopyBuffer(cmd, frame.addressTableStaging.buffer, frame.addressTableBuffer.buffer, 1, &addressCpy);
	}, frame.transferPool, true);

	frame.transferCmds = DeferredCmdSubmitQueue::collectTransfer();
	auto& sync = Renderer::_transferSync;
	frame.transferFence = transferQueue.submitWithSyncTimeline(
		frame.transferCmds,
		sync.semaphore,
		sync.signalValue
	);
	frame.transferWaitValue = sync.signalValue;
	fmt::print("[Transfer Submit] Timeline Value: {}\n", sync.signalValue);
	sync.signalValue++;

	auto fence = frame.transferFence;
	frame.deletionQueue.push_function([&, fence, instStaging, indirectStaging, allocator]() mutable {
		transferQueue.fencePool.recycle(fence);
		BufferUtils::destroyBuffer(instStaging, allocator);
		BufferUtils::destroyBuffer(indirectStaging, allocator);
	});

	fmt::print("Instance buffer size: {}\n", instBufferSize);
	fmt::print("Indirect buffer size: {}\n", indirectBufferSize);
	fmt::print("GPU address table:\n");
	fmt::print("  Instance buffer: 0x{:X}\n", frame.addressTable.instanceBuffer);
	fmt::print("  Indirect buffer: 0x{:X}\n", frame.addressTable.indirectCmdBuffer);
}


void Batching::buildAndSortBatches(
	const std::vector<RenderObject>& opaqueObjects,
	const std::vector<RenderObject>& transparentObjects,
	const std::vector<GPUDrawRange>& drawRanges,
	std::vector<SortedBatch>& outSortedOpaqueBatches,
	std::vector<RenderObject>& outSortedTransparent)
{
	fmt::print("=== Building and Sorting Batches ===\n");
	fmt::print("Opaque Object Count: {}\n", opaqueObjects.size());
	fmt::print("Transparent Object Count: {}\n", transparentObjects.size());

	std::unordered_map<BatchKey, std::vector<IndirectDrawCmd>, BatchKeyHash> opaqueBatches;

	constexpr uint32_t MAX_INDEX_COUNT = 1000000;
	constexpr uint32_t MAX_INDEX_OFFSET = 10000000;
	constexpr uint32_t MAX_VERTEX_OFFSET = 10000000;

	for (size_t i = 0; i < opaqueObjects.size(); ++i) {
		const RenderObject& obj = opaqueObjects[i];

		if (obj.drawRangeIndex >= drawRanges.size()) {
			fmt::print("WARNING: Skipping RenderObject[{}] — drawRangeIndex={} out of bounds (max={})\n",
				i, obj.drawRangeIndex, drawRanges.size() - 1);
			continue;
		}

		const auto& range = drawRanges[obj.drawRangeIndex];

		if (range.indexCount == 0 || range.indexCount > MAX_INDEX_COUNT ||
			range.firstIndex > MAX_INDEX_OFFSET ||
			range.vertexOffset > MAX_VERTEX_OFFSET) {
			fmt::print("WARNING: Skipping RenderObject[{}] — invalid draw range values: indexCount={}, firstIndex={}, vertexOffset={}\n",
				i, range.indexCount, range.firstIndex, range.vertexOffset);
			continue;
		}

		BatchKey key{ obj.passType, obj.materialIndex, obj.drawRangeIndex };
		auto& cmdList = opaqueBatches[key];

		VkDrawIndexedIndirectCommand cmd = {
			.indexCount = range.indexCount,
			.instanceCount = 1,
			.firstIndex = range.firstIndex,
			.vertexOffset = static_cast<int32_t>(range.vertexOffset),
			.firstInstance = obj.instanceIndex
		};

		fmt::print("[Opaque {:>2}] MaterialIdx: {}, RangeIdx: {}, Cmd: [indexCount={}, firstIndex={}, vertexOffset={}, firstInstance={}]\n",
			i, obj.materialIndex, obj.drawRangeIndex,
			cmd.indexCount, cmd.firstIndex, cmd.vertexOffset, cmd.firstInstance
		);

		cmdList.push_back({ cmd, obj.instanceIndex });
	}

	uint32_t batchOffset = 0;
	for (const auto& [key, cmdList] : opaqueBatches) {
		if (cmdList.empty()) continue;

		SortedBatch batch;
		batch.pipeline = getPipelineForPass(key.passType);
		if (!batch.pipeline) {
			fmt::print("ERROR: No pipeline for passType={} — skipping batch\n", static_cast<uint8_t>(key.passType));
			continue;
		}

		batch.drawOffset = batchOffset;

		const auto& firstCmd = cmdList.front().cmd;

		VkDrawIndexedIndirectCommand draw{};
		draw.indexCount = firstCmd.indexCount;
		draw.firstIndex = firstCmd.firstIndex;
		draw.vertexOffset = firstCmd.vertexOffset;
		draw.firstInstance = batchOffset;
		draw.instanceCount = static_cast<uint32_t>(cmdList.size());

		batch.cmds = { { draw, draw.firstInstance } };

		outSortedOpaqueBatches.push_back(batch);
		batchOffset++;
	}

	fmt::print("Total Sorted Opaque Batches: {}\n", outSortedOpaqueBatches.size());

	outSortedTransparent = transparentObjects;

	if (!outSortedTransparent.empty()) {
		glm::vec3 camPos = glm::vec3(RenderScene::getCurrentSceneData().cameraPosition);
		std::sort(outSortedTransparent.begin(), outSortedTransparent.end(),
			[&](const RenderObject& A, const RenderObject& B) {
				glm::vec3 centerA = (A.aabb.vmin + A.aabb.vmax) * 0.5f;
				glm::vec3 centerB = (B.aabb.vmin + B.aabb.vmax) * 0.5f;

				float distA = glm::length(centerA - camPos);
				float distB = glm::length(centerB - camPos);

				return distA > distB; // back to front
			}
		);
	}

	fmt::print("Sorted Transparent Object Count: {}\n", outSortedTransparent.size());
	fmt::print("=== End Batching ===\n\n");
}


GraphicsPipeline* Batching::getPipelineForPass(MaterialPass pass) {
	switch (pass) {
	case MaterialPass::Opaque:   return &Pipelines::opaquePipeline;
	case MaterialPass::Transparent: return &Pipelines::transparentPipeline;
	case MaterialPass::Wireframe:   return &Pipelines::wireframePipeline;
	case MaterialPass::Overlay:     return &Pipelines::boundingBoxPipeline;
	default: return nullptr;
	}
}
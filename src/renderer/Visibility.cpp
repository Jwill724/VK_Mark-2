#include "pch.h"

#include "Visibility.h"
#include "renderer/gpu_types/PipelineManager.h"

// GPU culling
void Visibility::performCulling(
	VkCommandBuffer cmd,
	CullingPushConstantsAddrs& cullingData,
	VkBuffer visibleCountStaging,
	VkBuffer visibleMeshIDsStaging,
	VkBuffer gpuVisibleCountBuffer,
	VkBuffer gpuVisibleMeshIDsBuffer,
	VkDescriptorSet sets[2]) {
	auto pcInfo = Pipelines::_globalLayout.pcRange;
	auto layout = Pipelines::_globalLayout.layout;

	// === Reset write-only output buffers ===
	VkDeviceSize countSize = sizeof(uint32_t);
	VkDeviceSize meshIDsSize = static_cast<VkDeviceSize>(cullingData.meshCount * sizeof(uint32_t));

	vkCmdFillBuffer(cmd, visibleMeshIDsStaging, 0, meshIDsSize, 0);

	vkCmdFillBuffer(cmd, visibleCountStaging, 0, countSize, 0);

	VkBufferMemoryBarrier2 resetBarriers[] = {
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
			.buffer = visibleCountStaging,
			.offset = 0,
			.size = countSize,
		},
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
			.buffer = visibleMeshIDsStaging,
			.offset = 0,
			.size = meshIDsSize,
		}
	};

	VkDependencyInfo resetDepInfo{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.bufferMemoryBarrierCount = 2,
		.pBufferMemoryBarriers = resetBarriers,
	};

	vkCmdPipelineBarrier2(cmd, &resetDepInfo);

	VkMemoryBarrier2 barrierPre{
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
		.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
		.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
	};
	VkDependencyInfo depInfoPre{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.memoryBarrierCount = 1,
		.pMemoryBarriers = &barrierPre,
	};
	vkCmdPipelineBarrier2(cmd, &depInfoPre);

	// === Compute Dispatch ===
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, Pipelines::visibilityPipeline.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 2, sets, 0, nullptr);
	vkCmdPushConstants(cmd, layout, pcInfo.stageFlags, pcInfo.offset, pcInfo.size, &cullingData);

	uint32_t groupCountX = (cullingData.meshCount + LOCAL_SIZE_X - 1) / LOCAL_SIZE_X;
	vkCmdDispatch(cmd, groupCountX, 1, 1);

	VkMemoryBarrier2 barrierPost{
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
		.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
		.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
	};
	VkDependencyInfo depInfoPost{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.memoryBarrierCount = 1,
		.pMemoryBarriers = &barrierPost,
	};
	vkCmdPipelineBarrier2(cmd, &depInfoPost);


	VkBufferCopy countCopy = {
		.srcOffset = 0,
		.dstOffset = 0,
		.size = countSize
	};
	vkCmdCopyBuffer(
		cmd,
		gpuVisibleCountBuffer,
		visibleCountStaging,
		1,
		&countCopy
	);

	VkBufferCopy meshIDsCopy = {
		.srcOffset = 0,
		.dstOffset = 0,
		.size = meshIDsSize
	};
	vkCmdCopyBuffer(
		cmd,
		gpuVisibleMeshIDsBuffer,
		visibleMeshIDsStaging,
		1,
		&meshIDsCopy
	);

	if (cullingData.rebuildTransforms == 1) {
		cullingData.rebuildTransforms = 0;
	}
}


// CPU Sided culling
// Frustum culling
bool Visibility::isVisible(const AABB aabb, const Frustum frus) {
	if (!boxInFrustum(aabb, frus)) {
		return false;
	}

	return true; // object is in the view frustum
}

bool Visibility::boxInFrustum(const AABB box, const Frustum fru) {
	const glm::vec3 center = (box.vmax + box.vmin) * 0.5f;
	const glm::vec3 extents = (box.vmax - box.vmin) * 0.5f;

	const float minSafeRadius = box.sphereRadius * 0.01f;
	const float safeRadius = glm::max(box.sphereRadius, minSafeRadius);

	//For each plane in the frustum
	for (int i = 0; i < 6; i++) {
		glm::vec3 normal = glm::vec3(fru.planes[i]);
		float d = fru.planes[i].w;

		float dist = glm::dot(normal, center) + d;

		// FIXME: need to adjust culling issues with small spheres
		if (dist < -safeRadius) return false;

		float r =
			extents.x * abs(normal.x) +
			extents.y * abs(normal.y) +
			extents.z * abs(normal.z);

		if (dist + r < 0.0f) return false;
	}

	int out;

	// check +x
	out = 0;
	for (int i = 0; i < 8; i++)
		out += (fru.points[i].x > box.vmax.x) ? 1 : 0;
	if (out == 8) return false;

	// check -x
	out = 0;
	for (int i = 0; i < 8; i++)
		out += (fru.points[i].x < box.vmin.x) ? 1 : 0;
	if (out == 8) return false;

	// check +y
	out = 0;
	for (int i = 0; i < 8; i++)
		out += (fru.points[i].y > box.vmax.y) ? 1 : 0;
	if (out == 8) return false;

	// check -y
	out = 0;
	for (int i = 0; i < 8; i++)
		out += (fru.points[i].y < box.vmin.y) ? 1 : 0;
	if (out == 8) return false;

	// check +z
	out = 0;
	for (int i = 0; i < 8; i++)
		out += (fru.points[i].z > box.vmax.z) ? 1 : 0;
	if (out == 8) return false;

	// check -z
	out = 0;
	for (int i = 0; i < 8; i++)
		out += (fru.points[i].z < box.vmin.z) ? 1 : 0;
	if (out == 8) return false;

	return true;
}

Frustum Visibility::extractFrustum(const glm::mat4& viewproj) {
	const glm::mat4 vpt = glm::transpose(viewproj);

	Frustum frustum{};
	frustum.planes[0] = vpt[3] + vpt[0]; // left
	frustum.planes[1] = vpt[3] - vpt[0]; // right
	frustum.planes[2] = vpt[3] + vpt[1]; // bot
	frustum.planes[3] = vpt[3] - vpt[1]; // top
	frustum.planes[4] = vpt[3] + vpt[2]; // near
	frustum.planes[5] = vpt[3] - vpt[2]; // far

	for (int i = 0; i < 6; ++i) {
		frustum.planes[i] /= glm::length(glm::vec3(frustum.planes[i]));
	}

	glm::mat4 invVp = glm::inverse(viewproj);
	int i = 0;
	for (int x = -1; x <= 1; x += 2) {
		for (int y = -1; y <= 1; y += 2) {
			for (int z = -1; z <= 1; z += 2) {
				glm::vec4 corner = invVp * glm::vec4(x, y, z, 1.0f);
				frustum.points[i++] = glm::vec4(corner) / corner.w;
			}
		}
	}

	return frustum;
}


AABB Visibility::transformAABB(const AABB& localBox, const glm::mat4& transform) {
	// Convert to min/max corners first
	const glm::vec3 vmin = localBox.vmin;
	const glm::vec3 vmax = localBox.vmax;

	const glm::vec3 corners[8] = {
		glm::vec3(transform * glm::vec4(vmin.x, vmin.y, vmin.z, 1.0f)),
		glm::vec3(transform * glm::vec4(vmin.x, vmax.y, vmin.z, 1.0f)),
		glm::vec3(transform * glm::vec4(vmin.x, vmin.y, vmax.z, 1.0f)),
		glm::vec3(transform * glm::vec4(vmin.x, vmax.y, vmax.z, 1.0f)),
		glm::vec3(transform * glm::vec4(vmax.x, vmin.y, vmin.z, 1.0f)),
		glm::vec3(transform * glm::vec4(vmax.x, vmax.y, vmin.z, 1.0f)),
		glm::vec3(transform * glm::vec4(vmax.x, vmin.y, vmax.z, 1.0f)),
		glm::vec3(transform * glm::vec4(vmax.x, vmax.y, vmax.z, 1.0f))
	};

	// Now apply the min/max algorithm from before using the 8 transformed corners
	glm::vec3 newVmin = corners[0];
	glm::vec3 newVmax = newVmin;

	// Start looping from corner 1 onwards
	for (size_t i = 1; i < 8; ++i) {
		const auto& current = corners[i];
		newVmin = glm::min(newVmin, current);
		newVmax = glm::max(newVmax, current);
	}

	AABB worldBox{};
	worldBox.vmin = newVmin;
	worldBox.vmax = newVmax;
	worldBox.origin = (newVmax + newVmin) * 0.5f;
	worldBox.extent = (newVmax - newVmin) * 0.5f;
	worldBox.sphereRadius = glm::length(worldBox.extent);

	return worldBox;
}

std::vector<glm::vec3> Visibility::GetAABBVertices(const AABB& box) {
	const glm::vec3 vmin = box.vmin;
	const glm::vec3 vmax = box.vmax;

	const glm::vec3 corners[8] = {
		glm::vec3(vmin.x, vmin.y, vmin.z),
		glm::vec3(vmin.x, vmax.y, vmin.z),
		glm::vec3(vmin.x, vmin.y, vmax.z),
		glm::vec3(vmin.x, vmax.y, vmax.z),
		glm::vec3(vmax.x, vmin.y, vmin.z),
		glm::vec3(vmax.x, vmax.y, vmin.z),
		glm::vec3(vmax.x, vmin.y, vmax.z),
		glm::vec3(vmax.x, vmax.y, vmax.z)
	};

	// Now connect the corners to form 12 lines
	std::vector<glm::vec3> vertices = {
		corners[0], corners[1],
		corners[2], corners[3],
		corners[4], corners[5],
		corners[6], corners[7],

		corners[0], corners[2],
		corners[1], corners[3],
		corners[4], corners[6],
		corners[5], corners[7],

		corners[0], corners[4],
		corners[1], corners[5],
		corners[2], corners[6],
		corners[3], corners[7]
	};

	return vertices;
}
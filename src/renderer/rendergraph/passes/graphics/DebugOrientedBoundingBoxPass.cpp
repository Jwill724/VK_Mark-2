#include "pch.h"

#include "../../RenderPasses.h"
#include "../../scopes/GraphicsScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../backend/memory/BindlessBDATable.h"
#include "../../../../profiler/Profiler.h"
#include "../../../frame/FrameContext.h"

static constexpr size_t PIPE_ID_MAIN = 0;

static struct alignas(16) OBBPush
{
	VkDeviceAddress vertexBuffer;
	uint32_t pad0[2];
};

void RegisterOBBLineDebugPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"OBB_Line_Debug",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.ReadResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)

				.WriteResource(
					RD::Renderer_RenderTarget::Opaque,
					RD::ImageAccess::GraphicsColorWrite,
					RD::ImageAccess::Read)

				.SetSetup(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						pass.scope = GraphicsScope{};
						auto& pso = std::get<GraphicsScope>(pass.scope);

						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& opaque = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Opaque);

						AttachmentDesc opaqueAttach{};
						opaqueAttach.imageView = opaque.m_imageView;
						opaqueAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

						AttachmentDesc depthAttach{};
						depthAttach.imageView = depthResolved.m_imageView;
						depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
						depthAttach.clearValue.depthStencil.depth = 0.0f;

						pso.UpdateRenderInfo(
							{
								opaque.Width(),
								opaque.Height()
							},
							{ opaqueAttach, depthAttach });
					})

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return ctx.frameState->bHasVisibles &&
							ctx.profiler->debugToggles.enableOBBs;
					})

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::OBBLineView,
							pass.passName);

						auto& pso = std::get<GraphicsScope>(pass.scope);
						const auto& frameCtx = ctx.frameCtx;

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& obbBuffer = frameCtx->GetOBBLineDebugBuffer();

						OBBPush obbPush{ .vertexBuffer = obbBuffer.m_address };
						pso.SetPush(obbPush);

						pso.BeginRendering(cmd);

						pso.DrawVertexPull(
							cmd,
							obbBuffer.m_buffer,
							0,
							frameCtx->GetOBBDrawOffsets(),
							pass.pipelines[PIPE_ID_MAIN]);

						pso.EndRendering(cmd);
					});
		});
}


//std::vector<glm::vec3> allVerts;
//std::vector<uint32_t> drawOffsets;

//auto& resources = Engine::GetState().getGPUResources();
//const auto& meshes = resources.GetResgisteredMeshes().meshData;

//auto emitOBBVerts = [&](const Instance& inst) {
//	const auto& aabb = meshes[inst.meshID].localAABB;
//	const auto& matrix = World::_globalTransforms[inst.transformID];
//	auto verts = GetOBBVertices(aabb, matrix);
//	uint32_t offset = static_cast<uint32_t>(allVerts.size());
//	drawOffsets.push_back(offset);
//	allVerts.insert(allVerts.end(), verts.begin(), verts.end());
//};
//for (const auto& inst : frameCtx.m_visibleInstances) emitOBBVerts(inst);

//const auto allocator = resources.GetAllocator();

//const size_t totalSize = allVerts.size() * sizeof(glm::vec3);

//AllocatedBuffer obbVBO = BufferUtils::CreateBuffer(
//	totalSize,
//	VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
//	VMA_MEMORY_USAGE_CPU_TO_GPU,
//	allocator);
//ASSERT(obbVBO.m_allocInfo.pMappedData != nullptr);
//memcpy(obbVBO.m_mappedPtr, allVerts.data(), totalSize);

//auto aabbBuf = obbVBO.m_buffer;
//auto aabbAlloc = obbVBO.m_allocation;
//frameCtx.m_cpuDeletionQueue.PushFunction([aabbBuf, aabbAlloc, allocator]() mutable {
//	BufferUtils::DestroyBuffer(aabbBuf, aabbAlloc, allocator);
//});

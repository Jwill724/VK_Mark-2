#include "pch.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"
#include "scopes/ComputeScope.h"
#include "scopes/GraphicsScope.h"

RenderPassBuilder& RenderPassBuilder::ReadResource(
	RD::Renderer_RenderTarget target,
	RD::ImageAccess access,
	uint32_t baseMip,
	uint32_t mipCount)
{
	m_desc.resources.emplace_back(RenderResourceUsage{
		.target = target,
		.enterAccess = access,
		.exitAccess = access,
		.baseMip = baseMip,
		.mipCount = mipCount,
		.bIsWrite = false
	});

	return *this;
}

RenderPassBuilder& RenderPassBuilder::WriteResource(
	RD::Renderer_RenderTarget target,
	RD::ImageAccess enterAccess,
	RD::ImageAccess exitAccess,
	uint32_t baseMip,
	uint32_t mipCount)
{
	m_desc.resources.emplace_back(RenderResourceUsage{
		.target = target,
		.enterAccess = enterAccess,
		.exitAccess = exitAccess,
		.baseMip = baseMip,
		.mipCount = mipCount,
		.bIsWrite = true
	});

	return *this;
}

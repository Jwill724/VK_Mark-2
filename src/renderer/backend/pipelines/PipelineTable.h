#pragma once

#include "PipelineDefs.h"
#include "../../RendererDefinitions.h"

namespace RD = RendererDefinitions;

namespace PipelineTable
{
	void Build();
	const PipelineDef& Get(RD::Renderer_Pipeline id);
}
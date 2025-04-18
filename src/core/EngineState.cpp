#include "EngineState.h"

GraphicsPipeline* getPipelineByType(PipelineType type) {
	switch (type) {
	case PipelineType::Opaque: return &Pipelines::opaquePipeline;
	case PipelineType::Transparent: return &Pipelines::transparentPipeline;
	case PipelineType::Wireframe: return &Pipelines::wireframePipeline;
	case PipelineType::BoundingBoxes: return &Pipelines::boundingBoxPipeline;
	// case PipelineType::DebugNormals: return &Pipelines::debugNormalPipeline;
	default: return nullptr;
	}
}
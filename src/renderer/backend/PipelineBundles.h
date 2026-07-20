#pragma once

#include <array>
#include "../RendererDefinitions.h"

namespace RD = RendererDefinitions;

enum class DirectionalCSMPipelineSlot : uint8_t
{
	Shadow,
	Count
};


enum class FlashlightShadowPipelineSlot : uint8_t
{
	Shadow,

	Count
};

enum class BasePrepassPipelineSlot : uint8_t
{
	Prepass,

	Count
};

enum class HiZGenerationPipelineSlot : uint8_t
{
	Generate,

	Count
};

enum class SSAOPipelineSlot : uint8_t
{
	DepthPrefilter,

	Main,

	Filter,

	Denoise,

	Count
};

enum class SSContactShadowPipelineSlot : uint8_t
{
	Main,

	Count
};


enum class VolumetricLightingPipelineSlot : uint8_t
{
	Raymarch,

	Blur,

	Count
};

enum class BloomPipelineSlot : uint8_t
{
	Downsample,

	Upsample,

	Count
};

enum class ExposurePipelineSlot : uint8_t
{
	Reduce,

	Finalize,

	Count
};

enum class FinalCompositePipelineSlot : uint8_t
{
	Composite,

	Count
};

enum class ChromaticAberrationPipelineSlot : uint8_t
{
	Main,

	Count
};

enum class LensFlarePipelineSlot : uint8_t
{
	FlareBright,

	FlareGenerate,

	Count
};

enum class VisibleLightCullPipelineSlot: uint8_t
{
	Main,

	Count
};

enum class ClusteredLightsPipelineSlot : uint8_t
{
	TileSliceRanges,

	IndirectArgs,

	ClusterCounts,

	ClusterOffsets,

	ClusterScatterIDs,

	Count
};


enum class SMAAPipelineSlot : uint8_t
{
	EdgeDetection,

	BlendWeights,

	NeighborhoodBlend,

	Count
};


enum class CMAA2PipelineSlot : uint8_t
{
	BuildEdges,

	DispatchArgs,

	ProcessCandidates,

	DeferredResolve,

	Count
};

enum class TAAPipelineSlot : uint8_t
{
	Main,

	Count
};

enum class FXAAPipelineSlot : uint8_t
{
	Main,

	Count
};

enum class SkyboxPipelineSlot : uint8_t
{
	Main,

	Count
};


enum class TransparentResolvePipelineSlot : uint8_t
{
	Resolve,

	Count
};


enum class OpaqueForwardPipelineSlot : uint8_t
{
	Opaque,
	Wireframe,

	Count
};

enum class TransparentForwardPipelineSlot : uint8_t
{
	Transparent,

	Count
};

enum class LineDebugPipelineSlot : uint8_t
{
	Line,

	Count
};

enum class DebugBuildPipelineSlot : uint8_t
{
	DebugCount,
	DebugArgs,
	DebugBuild,

	Count
};


enum class ShadowBoundsPipelineSlot : uint8_t
{
	ShadowBounds,

	Count
};


enum class InstanceCullPipelineSlot : uint8_t
{
	Cull,

	Count
};

enum class DrawBuildPipelineSlot : uint8_t
{
	Args,
	Scatter,
	Emit,
	Place,

	Count
};


// =====================================================================================
// PIPELINE BUNDLE TRAITS
// =====================================================================================
//
// Maps:
//
//     Virtual Slot
//
// TO:
//
//     Real RD::Renderer_Pipeline
//
// =====================================================================================

template<typename SlotEnum>
struct PipelineBundleTraits;

template<>
struct PipelineBundleTraits<DirectionalCSMPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::Shadow
	};
};

template<>
struct PipelineBundleTraits<FlashlightShadowPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::Shadow
	};
};


template<>
struct PipelineBundleTraits<BasePrepassPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::Prepass
	};
};


template<>
struct PipelineBundleTraits<HiZGenerationPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::HiZGen
	};
};


template<>
struct PipelineBundleTraits<SSAOPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::SSAODepthPrefilter,
		RD::Renderer_Pipeline::SSAO,
		RD::Renderer_Pipeline::SSAOFilter,
		RD::Renderer_Pipeline::SSAODenoise
	};
};


template<>
struct PipelineBundleTraits<SSContactShadowPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::ScreenSpaceContactShadows
	};
};


template<>
struct PipelineBundleTraits<VolumetricLightingPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::VolumetricLight,
		RD::Renderer_Pipeline::VolumetricLightBlur
	};
};

template<>
struct PipelineBundleTraits<ExposurePipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::ExposureReduce,
		RD::Renderer_Pipeline::ExposureFinalize
	};
};

template<>
struct PipelineBundleTraits<FinalCompositePipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::FinalComposite
	};
};

template<>
struct PipelineBundleTraits<ChromaticAberrationPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::ChromaticAberration
	};
};

template<>
struct PipelineBundleTraits<LensFlarePipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::FlareBright,
		RD::Renderer_Pipeline::FlareGen
	};
};

template<>
struct PipelineBundleTraits<BloomPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::BloomDownsample,
		RD::Renderer_Pipeline::BloomUpsample
	};
};

template<>
struct PipelineBundleTraits<VisibleLightCullPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::LightCulling,
	};
};


template<>
struct PipelineBundleTraits<ClusteredLightsPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::ClusterTileSliceRanges,
		RD::Renderer_Pipeline::IndirectArgsLight,
		RD::Renderer_Pipeline::ClusterCount,
		RD::Renderer_Pipeline::ClusterScanOffsets,
		RD::Renderer_Pipeline::ClusterScatterIDs
	};
};

template<>
struct PipelineBundleTraits<SMAAPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::SMAAEdges,
		RD::Renderer_Pipeline::SMAAWeights,
		RD::Renderer_Pipeline::SMAABlend
	};
};


template<>
struct PipelineBundleTraits<CMAA2PipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::CMAA2Edges,
		RD::Renderer_Pipeline::CMAA2DispatchArgs,
		RD::Renderer_Pipeline::CMAA2ShapeCandidates,
		RD::Renderer_Pipeline::CMAA2DeferredResolve
	};
};

template<>
struct PipelineBundleTraits<TAAPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::TAA
	};
};

template<>
struct PipelineBundleTraits<FXAAPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::FXAA
	};
};

template<>
struct PipelineBundleTraits<SkyboxPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::Skybox
	};
};

template<>
struct PipelineBundleTraits<TransparentResolvePipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::TransparentResolve
	};
};


template<>
struct PipelineBundleTraits<OpaqueForwardPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::Opaque,
		RD::Renderer_Pipeline::Wireframe
	};
};

template<>
struct PipelineBundleTraits<TransparentForwardPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::Transparent
	};
};


template<>
struct PipelineBundleTraits<LineDebugPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::LineDebug
	};
};

template<>
struct PipelineBundleTraits<DebugBuildPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::DebugCount,
		RD::Renderer_Pipeline::DebugArgs,
		RD::Renderer_Pipeline::DebugBuild,
	};
};


template<>
struct PipelineBundleTraits<ShadowBoundsPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::ShadowBounds
	};
};


template<>
struct PipelineBundleTraits<InstanceCullPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::InstanceCull
	};
};


template<>
struct PipelineBundleTraits<DrawBuildPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::DrawArgs,
		RD::Renderer_Pipeline::DrawScatter,
		RD::Renderer_Pipeline::DrawEmit,
		RD::Renderer_Pipeline::DrawPlace
	};
};

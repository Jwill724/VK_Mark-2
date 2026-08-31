#pragma once

#include <array>
#include "../RendererDefinitions.h"

namespace RD = RendererDefinitions;

enum class DirectionalCSMPipelineSlot : uint8_t
{
	ShadowMesh,

	Count
};

enum class RTSunShadowPipelineSlot : uint8_t
{
	NRDShadowPrepare,
	RTSunShadow,

	Count
};

enum class FlashlightShadowPipelineSlot : uint8_t
{
	ShadowMeshMaskedD32,

	Count
};

enum class VolumetricShadowPipelineSlot : uint8_t
{
	ShadowMeshMaskedD16,

	Count
};


enum class BasePrepassPipelineSlot : uint8_t
{
	PrepassMesh,
	PrepassMaskedMesh,

	Count
};

enum class HiZGenerationPipelineSlot : uint8_t
{
	Generate,

	Count
};

enum class MaterialResolvePipelineSlot : uint8_t
{
	Resolve,

	Count
};

enum class VelocityResolvePipelineSlot : uint8_t
{
	Resolve,

	Count
};

enum class OpaqueLightingPipelineSlot : uint8_t
{
	Lighting,

	Count
};

enum class SSGIPipelineSlot : uint8_t
{
	HiZPrefilter,
	Main,

	GIAccum,

	Upsample,

	AODenoise,
	GIDenoise,

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
	TemporalResolve,
	Blur,

	Count
};

enum class BloomPipelineSlot : uint8_t
{
	OpaqueDownsample,

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

enum class ClusteredLightsPipelineSlot : uint8_t
{
	LightCull,

	TransparentClusterBounds,

	TileSliceRanges,

	IndirectArgs,

	ClusterCounts,

	ClusterOffsets,

	ClusterScatterIDs,

	Count
};


enum class TAAPipelineSlot : uint8_t
{
	Main,

	Count
};

enum class GBufferDebugPipelineSlot : uint8_t
{
	Main,

	Count
};

enum class SkyboxPipelineSlot : uint8_t
{
	Main,

	Count
};


enum class HDRSceneCompositePipelineSlot : uint8_t
{
	Composite,

	Count
};

enum class OpaqueWireframePipelineSlot : uint8_t
{
	WireframeMesh,

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

enum class TlasInstancesPipelineSlot { Build, Count };

enum class RTReflectionsPipelineSlot
{
	NRDReflectPrepare,
	Classify,
	Args,
	Intersect,
	Count
};

//enum class SMAAPipelineSlot : uint8_t
//{
//	EdgeDetection,
//
//	BlendWeights,
//
//	NeighborhoodBlend,
//
//	Count
//};


//enum class CMAA2PipelineSlot : uint8_t
//{
//	BuildEdges,
//
//	DispatchArgs,
//
//	ProcessCandidates,
//
//	DeferredResolve,
//
//	Count
//};

//enum class FXAAPipelineSlot : uint8_t
//{
//	Main,
//
//	Count
//};


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
struct PipelineBundleTraits<BasePrepassPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::PrepassMesh,
		RD::Renderer_Pipeline::PrepassMaskedMesh
	};
};

template<>
struct PipelineBundleTraits<DirectionalCSMPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::ShadowMesh
	};
};

template<>
struct PipelineBundleTraits<FlashlightShadowPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::ShadowMeshMaskedD32
	};
};

template<>
struct PipelineBundleTraits<VolumetricShadowPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::ShadowMeshMaskedD16
	};
};


template<>
struct PipelineBundleTraits<GBufferDebugPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::GBufferDebug
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
struct PipelineBundleTraits<SSGIPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::HiZPrefilter,
		RD::Renderer_Pipeline::VBGI,
		RD::Renderer_Pipeline::GIAccumulate,
		RD::Renderer_Pipeline::BilateralUpsample,
		RD::Renderer_Pipeline::AODenoise,
		RD::Renderer_Pipeline::GIDenoise
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
		RD::Renderer_Pipeline::VolumetricLightResolve,
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
struct PipelineBundleTraits<ClusteredLightsPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::LightCull,
		RD::Renderer_Pipeline::TransparentClusterBounds,
		RD::Renderer_Pipeline::ClusterTileSliceRanges,
		RD::Renderer_Pipeline::IndirectArgsLight,
		RD::Renderer_Pipeline::ClusterCount,
		RD::Renderer_Pipeline::ClusterScanOffsets,
		RD::Renderer_Pipeline::ClusterScatterIDs
	};
};

template<>
struct PipelineBundleTraits<MaterialResolvePipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::MaterialResolve
	};
};

template<>
struct PipelineBundleTraits<OpaqueLightingPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::OpaqueLighting
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
struct PipelineBundleTraits<SkyboxPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::Skybox
	};
};

template<>
struct PipelineBundleTraits<HDRSceneCompositePipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::HDRSceneComposite
	};
};

template<>
struct PipelineBundleTraits<VelocityResolvePipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::VelocityResolve
	};
};

template<>
struct PipelineBundleTraits<OpaqueWireframePipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::WireframeMesh
	};
};

template<>
struct PipelineBundleTraits<TransparentForwardPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::TransparentForward
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

template<>
struct PipelineBundleTraits<TlasInstancesPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::TlasInstances
	};
};

template<>
struct PipelineBundleTraits<RTSunShadowPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::NRDPrepare,
		RD::Renderer_Pipeline::RTShadowTrace,
	};
};

template<>
struct PipelineBundleTraits<RTReflectionsPipelineSlot>
{
	static constexpr std::array mappings =
	{
		RD::Renderer_Pipeline::NRDPrepare,
		RD::Renderer_Pipeline::ReflectClassify,
		RD::Renderer_Pipeline::RTRayArgs,
		RD::Renderer_Pipeline::RTReflectTrace
	};
};

//template<>
//struct PipelineBundleTraits<SMAAPipelineSlot>
//{
//	static constexpr std::array mappings =
//	{
//		RD::Renderer_Pipeline::SMAAEdges,
//		RD::Renderer_Pipeline::SMAAWeights,
//		RD::Renderer_Pipeline::SMAABlend
//	};
//};

//template<>
//struct PipelineBundleTraits<CMAA2PipelineSlot>
//{
//	static constexpr std::array mappings =
//	{
//		RD::Renderer_Pipeline::CMAA2Edges,
//		RD::Renderer_Pipeline::CMAA2DispatchArgs,
//		RD::Renderer_Pipeline::CMAA2ShapeCandidates,
//		RD::Renderer_Pipeline::CMAA2DeferredResolve
//	};
//};
//
//template<>
//struct PipelineBundleTraits<FXAAPipelineSlot>
//{
//	static constexpr std::array mappings =
//	{
//		RD::Renderer_Pipeline::FXAA
//	};
//};

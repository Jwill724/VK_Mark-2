#include "pch.h"

#include "../../RenderPasses.h"
#include "../../scopes/ComputeScope.h"
#include "../../../backend/ImageUtils.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../backend/DescriptorWriter.h"

namespace I = ImageUtils;

static constexpr size_t PIPE_ID_HDR_CUBEMAP = 0;
static constexpr size_t PIPE_ID_IRRADIANCE  = 1;
static constexpr size_t PIPE_ID_SPECULAR    = 2;
static constexpr size_t PIPE_ID_BRDF        = 3;

static struct alignas(16) EnvPush
{
	float    sampleCountF = 0.0f;
	uint32_t sampleCountU = 0u;
	uint32_t pad0[2]{};
};

void BakeEnvironmentMaps(
	VkCommandBuffer cmd,
	BindlessImageTable& imageTable,
	std::span<const PipelineHandle> pipelines)
{
	ASSERT(pipelines.size() >= PIPE_ID_BRDF + 1);

	ComputeScope pso{{}};

	PushDescriptorWriter pushWriter;

	const auto skyboxSampler     = imageTable.GetSampler(RD::Renderer_Sampler::Skybox);
	const auto irradianceSampler = imageTable.GetSampler(RD::Renderer_Sampler::Irradiance);
	const auto specSampler       = imageTable.GetSampler(RD::Renderer_Sampler::Specular);
	const auto& brdf             = imageTable.GetStaticTexture(RD::Renderer_Texture::Brdf);

	const auto setCount = imageTable.EnvironmentSetCount();

	EnvPush envPush;
	envPush.sampleCountU = RD::PREFILTER_SAMPLE_COUNT;
	envPush.sampleCountF = RD::DIFFUSE_SAMPLE_DELTA;

	for (uint32_t i = 0; i < setCount; ++i)
	{
		const auto& envSet = imageTable.GetEnvironmentSet(i);
		ASSERT(envSet.IsValid());

		auto& equirect   = envSet.equirect;
		auto& irradiance = envSet.irradiance;
		auto& specular   = envSet.specular;
		auto& skybox     = envSet.skybox;

		I::TransitionLayout(cmd, skybox,     RD::ImageAccess::Undefined,   RD::ImageAccess::Write);
		I::TransitionLayout(cmd, specular,   RD::ImageAccess::Undefined,   RD::ImageAccess::Write);
		I::TransitionLayout(cmd, irradiance, RD::ImageAccess::Undefined,   RD::ImageAccess::Write);

		// =========================
		// HDR Equirect to cubemap
		// =========================

		pso.BindReadImage(pushWriter,  RD::PUSH_BINDING_READ_1,  equirect, skyboxSampler);
		pso.BindWriteImage(pushWriter, RD::PUSH_BINDING_WRITE_1, skybox, 0);

		pso.UpdateExtent({ skybox.Width(), skybox.Height() });
		pso.UpdateWorkgroups({ 16, 16, 6 });

		pso.DispatchComputePass(cmd, pipelines[PIPE_ID_HDR_CUBEMAP], pushWriter);

		I::TransitionLayout(cmd, skybox, RD::ImageAccess::Write, RD::ImageAccess::Read);
		I::GenerateCubemapMipLevels(cmd, skybox);

		// ======================
		// Irradiance diffuse
		// ======================

		pso.BindReadImage(pushWriter,  RD::PUSH_BINDING_READ_1,  skybox,     irradianceSampler);
		pso.BindWriteImage(pushWriter, RD::PUSH_BINDING_WRITE_1, irradiance, 0);
		pso.SetPush(envPush);

		pso.UpdateExtent({ irradiance.Width(), irradiance.Height() });
		pso.UpdateWorkgroups({ 8, 8, 6 });

		pso.DispatchComputePass(cmd, pipelines[PIPE_ID_IRRADIANCE], pushWriter);

		I::TransitionLayout(cmd, irradiance, RD::ImageAccess::Write, RD::ImageAccess::Read);

		// ========================
		// Specular prefilter
		// ========================

		for (uint32_t mip = 0; mip < specular.m_mipLevels; ++mip)
		{
			pso.BindReadImage(pushWriter,  RD::PUSH_BINDING_READ_1,  skybox,  specSampler);
			pso.BindWriteImage(pushWriter, RD::PUSH_BINDING_WRITE_1, specular, mip);

			pso.UpdateExtent({ envSet.specularPCs[mip].width, envSet.specularPCs[mip].height });
			pso.SetPush(envSet.specularPCs[mip]);

			pso.DispatchComputePass(cmd, pipelines[PIPE_ID_SPECULAR], pushWriter);
		}

		I::TransitionLayout(cmd, specular, RD::ImageAccess::Write, RD::ImageAccess::Read);
	}

	// ======================
	// BRDF LUT
	// ======================

	I::TransitionLayout(cmd, brdf, RD::ImageAccess::Undefined, RD::ImageAccess::Write);

	pso.BindWriteImage(pushWriter, RD::PUSH_BINDING_WRITE_1, brdf);
	pso.UpdateExtent({ brdf.Width(), brdf.Height() });
	pso.UpdateWorkgroups(WORKGROUP_8x8);
	pso.SetPush(envPush);

	pso.DispatchComputePass(cmd, pipelines[PIPE_ID_BRDF], pushWriter);

	I::TransitionLayout(cmd, brdf, RD::ImageAccess::Write, RD::ImageAccess::Read);
}

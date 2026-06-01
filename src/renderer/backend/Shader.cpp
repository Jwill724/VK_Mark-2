#include "pch.h"

#include "Shader.h"
#include <unordered_map>
#include <fstream>

static const std::unordered_map<RD::Renderer_Shader, std::string> shaderPaths
{
	{ RD::Renderer_Shader::Opaque_v,                    "core/forwardVS.spv"                        },
	{ RD::Renderer_Shader::Opaque_f,                    "core/opaque_forward.spv"                   },
	{ RD::Renderer_Shader::Transparent_f,               "core/transparent_forward.spv"              },
	{ RD::Renderer_Shader::TransparentResolve_c,        "core/transparent_resolve.spv"              },
	{ RD::Renderer_Shader::ObbLine_v,                   "debug/obb_lineVS.spv"                      },
	{ RD::Renderer_Shader::ObbLine_f,                   "debug/obb_lineFS.spv"                      },
	{ RD::Renderer_Shader::Skybox_v,                    "environment/skyboxVS.spv"                  },
	{ RD::Renderer_Shader::Skybox_f,                    "environment/skyboxFS.spv"                  },
	{ RD::Renderer_Shader::Prepass_v,                   "core/prepassVS.spv"                        },
	{ RD::Renderer_Shader::Prepass_f,                   "core/prepassFS.spv"                        },
	{ RD::Renderer_Shader::Shadow_v,                    "shadows/shadow_depthVS.spv"                },
	{ RD::Renderer_Shader::ExposureReduce_c,            "post_process/exposure_reduce.spv"          },
	{ RD::Renderer_Shader::ExposureFinalize_c,          "post_process/exposure_finalize.spv"        },
	{ RD::Renderer_Shader::FinalComposite_c,            "post_process/final_composite.spv"          },
	{ RD::Renderer_Shader::HiZGen_c,                    "core/hi_z_gen.spv"                         },
	{ RD::Renderer_Shader::HDRToCubemap_c,              "environment/hdr2cubemap.spv"               },
	{ RD::Renderer_Shader::SpecularPrefilter_c,         "environment/specular_prefilter.spv"        },
	{ RD::Renderer_Shader::DiffuseIrradiance_c,         "environment/diffuse_irradiance.spv"        },
	{ RD::Renderer_Shader::BRDFLUT_c,                   "environment/brdf_lut.spv"                  },
	{ RD::Renderer_Shader::SSAO_c,                      "ao/ssao_main.spv"                          },
	{ RD::Renderer_Shader::SSAOFilter_c,                "ao/ssao_filter.spv"                        },
	{ RD::Renderer_Shader::SSAODenoise_c,               "ao/ssao_denoise.spv"                       },
	{ RD::Renderer_Shader::SSAODepthPrefilter_c,        "ao/ssao_depth_prefilter.spv"               },
	{ RD::Renderer_Shader::VolumetricLight_c,           "post_process/volumetric_light.spv"         },
	{ RD::Renderer_Shader::VolumetricLightBlur_c,       "post_process/volumetric_light_blur.spv"    },
	{ RD::Renderer_Shader::FlareBright_c,               "post_process/flare_bright.spv"             },
	{ RD::Renderer_Shader::FlareGen_c,                  "post_process/flare_gen.spv"                },
	{ RD::Renderer_Shader::SMAAEdges_c,                 "post_process/smaa_edges.spv"               },
	{ RD::Renderer_Shader::SMAAWeights_c,               "post_process/smaa_weights.spv"             },
	{ RD::Renderer_Shader::SMAABlend_c,                 "post_process/smaa_blend.spv"               },
	{ RD::Renderer_Shader::FXAA_c,                      "post_process/fxaa.spv"                     },
	{ RD::Renderer_Shader::TAA_c,                       "post_process/taa.spv"                      },
	{ RD::Renderer_Shader::CMAA2Edges_c,                "post_process/cmaa2_edges.spv"              },
	{ RD::Renderer_Shader::CMAA2ShapeCandidates_c,      "post_process/cmaa2_shape_candidates.spv"   },
	{ RD::Renderer_Shader::CMAA2DeferredResolve_c,      "post_process/cmaa2_deferred_resolve.spv"   },
	{ RD::Renderer_Shader::CMAA2DispatchArgs_c,         "post_process/cmaa2_indirect_args.spv"      },
	{ RD::Renderer_Shader::ClusterTileSliceRanges_c,    "clustered/cluster_tile_slice_ranges.spv"   },
	{ RD::Renderer_Shader::ClusterCount_c,              "clustered/cluster_count.spv"               },
	{ RD::Renderer_Shader::ClusterScanOffsets_c,        "clustered/cluster_scan_offsets.spv"        },
	{ RD::Renderer_Shader::ClusterScatterIDs_c,         "clustered/cluster_scatter_ids.spv"         },
	{ RD::Renderer_Shader::LightCulling_c,              "clustered/light_culling.spv"               },
	{ RD::Renderer_Shader::IndirectArgsLight_c,         "clustered/lights_indirect_args.spv"        },
	{ RD::Renderer_Shader::ScreenSpaceContactShadows_c, "shadows/bend_sss.spv"                      },
	{ RD::Renderer_Shader::ChromaticAberration_c,       "post_process/chromatic_aberration.spv"     }
};

const std::string& GetShaderPath(RD::Renderer_Shader id)
{
	return shaderPaths.at(id);
}

bool Shader::CreateModule(VkDevice device, VkShaderModule& outModule) const
{
	// Should move file loading functionality somewhere else
	std::ifstream file(m_path, std::ios::ate | std::ios::binary);
	if (!file.is_open()) return false;
	size_t fileSize = static_cast<size_t>(file.tellg());
	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
	file.seekg(0);
	file.read((char*)buffer.data(), fileSize);
	file.close();

	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;

	createInfo.codeSize = buffer.size() * sizeof(uint32_t);
	createInfo.pCode = buffer.data();

	VK_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, &outModule));
	return true;
}

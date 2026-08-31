#include "pch.h"

#include "Shader.h"
#include <unordered_map>
#include <fstream>

static const std::unordered_map<RD::Renderer_Shader, std::string> shaderPaths
{
	{ RD::Renderer_Shader::Prepass_f,                   "core/prepassFS.spv"                        },
	{ RD::Renderer_Shader::PrepassMasked_f,             "core/prepassFS_masked.spv"                 },
	{ RD::Renderer_Shader::MaterialResolve_c,           "core/material_resolve.spv"                 },
	{ RD::Renderer_Shader::VelocityResolve_c,           "core/velocity_resolve.spv"                 },
	{ RD::Renderer_Shader::OpaqueLighting_c,            "core/opaque_lighting.spv"                  },
	{ RD::Renderer_Shader::HiZGen_c,                    "core/hi_z_gen.spv"                         },
	{ RD::Renderer_Shader::TransparentCull_t,           "core/transparent_cull.spv"                 },
	{ RD::Renderer_Shader::TransparentDraw_m,           "core/transparent_draw.spv"                 },
	{ RD::Renderer_Shader::Transparent_f,               "core/transparent_forward.spv"              },
	{ RD::Renderer_Shader::HDRSceneComposite_c,         "core/hdr_scene_composite.spv"              },
	{ RD::Renderer_Shader::NRDPrepare_c,                "core/nrd_prepare.spv"                      },
	{ RD::Renderer_Shader::Wireframe_m,                 "debug/wireframeM.spv"                      },
	{ RD::Renderer_Shader::GBufferDebug_c,              "debug/gbuffer_view.spv"                    },
	{ RD::Renderer_Shader::LineDebug_v,                 "debug/line_debugVS.spv"                    },
	{ RD::Renderer_Shader::LineDebug_f,                 "debug/line_debugFS.spv"                    },
	{ RD::Renderer_Shader::DebugCount_c,                "debug/debug_count.spv"                     },
	{ RD::Renderer_Shader::DebugArgs_c,                 "debug/debug_args.spv"                      },
	{ RD::Renderer_Shader::DebugBuild_c,                "debug/debug_build.spv"                     },
	{ RD::Renderer_Shader::Wireframe_f,                 "debug/wireframeFS.spv"                     },
	{ RD::Renderer_Shader::Skybox_v,                    "environment/skyboxVS.spv"                  },
	{ RD::Renderer_Shader::Skybox_f,                    "environment/skyboxFS.spv"                  },
	{ RD::Renderer_Shader::ExposureReduce_c,            "post_process/exposure_reduce.spv"          },
	{ RD::Renderer_Shader::ExposureFinalize_c,          "post_process/exposure_finalize.spv"        },
	{ RD::Renderer_Shader::FinalComposite_c,            "post_process/final_composite.spv"          },
	{ RD::Renderer_Shader::HDRToCubemap_c,              "environment/hdr2cubemap.spv"               },
	{ RD::Renderer_Shader::SpecularPrefilter_c,         "environment/specular_prefilter.spv"        },
	{ RD::Renderer_Shader::SHIrradiance_c,              "environment/sh_irradiance.spv"             },
	{ RD::Renderer_Shader::BRDFLUT_c,                   "environment/brdf_lut.spv"                  },
	{ RD::Renderer_Shader::HiZPrefilter_c,              "ssgi/hi_z_prefilter.spv"                   },
	{ RD::Renderer_Shader::VBGI_c,                      "ssgi/vbgi_main.spv"                        },
	{ RD::Renderer_Shader::BilateralUpsample_c,         "ssgi/bilateral_upsample.spv"               },
	{ RD::Renderer_Shader::AODenoise_c,                 "ssgi/ao_denoise.spv"                       },
	{ RD::Renderer_Shader::GIDenoise_c,                 "ssgi/gi_denoise.spv"                       },
	{ RD::Renderer_Shader::GIAccumulate_c,              "ssgi/gi_accumulate.spv"                    },
	{ RD::Renderer_Shader::VolumetricLight_c,           "post_process/vol_light_raymarch.spv"       },
	{ RD::Renderer_Shader::VolumetricLightBlur_c,       "post_process/vol_light_blur.spv"           },
	{ RD::Renderer_Shader::VolumetricLightResolve_c,    "post_process/vol_light_resolve.spv"        },
	{ RD::Renderer_Shader::FlareBright_c,               "post_process/flare_bright.spv"             },
	{ RD::Renderer_Shader::FlareGen_c,                  "post_process/flare_gen.spv"                },
	{ RD::Renderer_Shader::BloomDownsample_c,           "post_process/bloom_downsample.spv"         },
	{ RD::Renderer_Shader::BloomUpsample_c,             "post_process/bloom_upsample.spv"           },
	{ RD::Renderer_Shader::TAA_c,                       "post_process/taa.spv"                      },
	{ RD::Renderer_Shader::ChromaticAberration_c,       "post_process/chromatic_aberration.spv"     },
	{ RD::Renderer_Shader::ClusterTileSliceRanges_c,    "clustered/cluster_tile_slice_ranges.spv"   },
	{ RD::Renderer_Shader::TransparentClusterBounds_c,  "clustered/transparent_cluster_bounds.spv"  },
	{ RD::Renderer_Shader::ClusterCount_c,              "clustered/cluster_count.spv"               },
	{ RD::Renderer_Shader::ClusterScanOffsets_c,        "clustered/cluster_scan_offsets.spv"        },
	{ RD::Renderer_Shader::ClusterScatterIDs_c,         "clustered/cluster_scatter_ids.spv"         },
	{ RD::Renderer_Shader::LightCulling_c,              "clustered/light_culling.spv"               },
	{ RD::Renderer_Shader::IndirectArgsLight_c,         "clustered/lights_indirect_args.spv"        },
	{ RD::Renderer_Shader::ShadowBounds_c,              "shadows/shadow_bounds.spv"                 },
	{ RD::Renderer_Shader::ScreenSpaceContactShadows_c, "shadows/bend_sss.spv"                      },
	{ RD::Renderer_Shader::Shadow_t,                    "shadows/shadowT.spv"                       },
	{ RD::Renderer_Shader::Shadow_m,                    "shadows/shadowM.spv"                       },
	{ RD::Renderer_Shader::ShadowMasked_m,              "shadows/shadow_masked.spv"                 },
	{ RD::Renderer_Shader::ShadowMasked_f,              "shadows/shadow_maskedFS.spv"               },
	{ RD::Renderer_Shader::MeshletCull_t,               "visibility/meshlet_cull.spv"               },
	{ RD::Renderer_Shader::Prepass_m,                   "visibility/prepass_mesh.spv"               },
	{ RD::Renderer_Shader::PrepassMasked_m,             "visibility/prepass_masked_mesh.spv"        },
	{ RD::Renderer_Shader::InstanceCull_c,              "visibility/instance_cull.spv"              },
	{ RD::Renderer_Shader::DrawArgs_c,                  "visibility/draw_args.spv"                  },
	{ RD::Renderer_Shader::DrawEmit_c,                  "visibility/draw_emit.spv"                  },
	{ RD::Renderer_Shader::DrawScatter_c,               "visibility/draw_scatter.spv"               },
	{ RD::Renderer_Shader::DrawPlace_c,                 "visibility/draw_place.spv"                 },
	{ RD::Renderer_Shader::TlasInstances_c,             "core/tlas_instances.spv"                   },
	{ RD::Renderer_Shader::RTRayArgs_c,                 "core/rt_ray_args.spv"                      },
	{ RD::Renderer_Shader::RTShadowTrace_c,             "shadows/rtshadow_trace.spv"                },
	{ RD::Renderer_Shader::ReflectClassify_c,           "reflections/reflect_classify.spv"          },
	{ RD::Renderer_Shader::RTReflectTrace_c,            "reflections/rtreflect_trace.spv"           },
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

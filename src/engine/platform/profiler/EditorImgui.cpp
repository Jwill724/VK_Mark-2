#include "pch.h"

#include "EditorImgui.h"
#include "renderer/scene/RenderScene.h"
#include "engine/platform/input/UserInput.h"
#include "renderer/backend/Backend.h"

static void MyWindowFocusCallback(GLFWwindow* window, int focused) {
	ImGui_ImplGlfw_WindowFocusCallback(window, focused); // Forward to ImGui
}

void EditorImgui::initImgui(DeletionQueue& queue) {
	VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
	pool_info.pPoolSizes = pool_sizes;

	const auto device = Backend::getDevice();

	VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(device, &pool_info, nullptr, &imguiPool));

	// this initializes the core structures of imgui
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable keyboard controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Enable gamepad controls
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; // Prevent ImGui from overriding the cursor
	io.IniFilename = nullptr; // Won't create imgui file

	auto window = Engine::getWindow();

	ImGui_ImplGlfw_InitForVulkan(window, true);

	// this initializes imgui for Vulkan
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = Backend::getInstance();
	init_info.PhysicalDevice = Backend::getPhysicalDevice();
	init_info.Device = device;
	init_info.Queue = Backend::getGraphicsQueue().queue;
	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.UseDynamicRendering = true;

	//dynamic rendering parameters for imgui to use
	init_info.PipelineRenderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &Backend::getSwapchainDef().format;

	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info);

	ImGui_ImplVulkan_CreateFontsTexture();

	glfwSetWindowFocusCallback(window, MyWindowFocusCallback);

	// add the destroy the imgui created structures
	queue.push_function([=]() {
		ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(device, imguiPool, nullptr);
	});
}

static void uploadDebugInlineIfDirty(Profiler& profiler) {
	const auto unifiedSet = DescriptorSetOverwatch::getUnifiedDescriptor().descriptorSet;

	static DebugToggles last{};
	const DebugToggles& cur = profiler.debugToggles;

	if (memcmp(&last, &cur, sizeof(DebugToggles)) != 0) {
		DescriptorWriter w;
		w.writeInlineUniform(
			GLOBAL_BINDING_DEBUG_INLINE,
			&cur,
			static_cast<uint32_t>(sizeof(DebugToggles)),
			Backend::getDevice(),
			unifiedSet);
		last = cur;
	}
}

void EditorImgui::renderImgui(Profiler& profiler) {
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	auto& stats = profiler.getStats();
	auto& dbg = profiler.debugToggles;

	// Stats
	if (dbg.enableStats) {
		const auto camera = RenderScene::getCamera();
		ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10.f, 10.f), ImGuiCond_Always, ImVec2(1, 0));
		if (ImGui::Begin("Stats", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Camera: %.2f %.2f %.2f", camera._position.x, camera._position.y, camera._position.z);
			ImGui::Text("FPS: %.1f", stats.fps.get());
			ImGui::Text("Frame: %.3f ms", stats.frameTime.get());
			ImGui::Text("Draw:  %.3f ms", stats.drawTime.get());
			ImGui::Text("Scene: %.3f ms", stats.sceneUpdateTime.get());
			ImGui::Text("Triangles: %llu", (unsigned long long)stats.triangleCount);
			ImGui::Text("VRAM: %llu / %llu MB",
				stats.vramStats.used / (1024ull * 1024ull),
				stats.vramStats.budget / (1024ull * 1024ull));
			ImGui::Text("GPU: %s", stats.gpuName.c_str());

			ImGui::Separator();
			ImGui::TextUnformatted("Model Data");
			ImGui::Text("Meshes:     %u", dbg.meshCount);
			ImGui::Text("Materials:  %u", dbg.materialCount);
			ImGui::Text("Transforms: %u", dbg.transformCount);
			ImGui::Text("Vertices:   %u", dbg.vertexCount);
			ImGui::Text("Indices:    %u", dbg.indexCount);

			ImGui::Separator();
			ImGui::TextUnformatted("Draw Calls");

			const uint32_t indirectTotal =
				stats.opaqueIndirect.commands + stats.transparentIndirect.commands;

			if (ImGui::BeginTable("DrawPathsTop", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn("API"); ImGui::TableSetupColumn("Commands"); ImGui::TableHeadersRow();
				ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("vkCmdDraw");
				ImGui::TableSetColumnIndex(1); ImGui::Text("%u", stats.directDraws);
				ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("vkCmdDrawIndirect");
				ImGui::TableSetColumnIndex(1); ImGui::Text("%u", indirectTotal);
				ImGui::EndTable();
			}

			if (ImGui::BeginTable("IndirectBreakdown", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn("Pass"); ImGui::TableSetupColumn("Commands"); ImGui::TableSetupColumn("Subdraws"); ImGui::TableHeadersRow();
				ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Opaque");
				ImGui::TableSetColumnIndex(1); ImGui::Text("%u", stats.opaqueIndirect.commands);
				ImGui::TableSetColumnIndex(2); ImGui::Text("%u", stats.opaqueIndirect.subdraws);
				ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Transparent");
				ImGui::TableSetColumnIndex(1); ImGui::Text("%u", stats.transparentIndirect.commands);
				ImGui::TableSetColumnIndex(2); ImGui::Text("%u", stats.transparentIndirect.subdraws);
				ImGui::EndTable();
			}
			ImGui::End();
		}
	}

	// Debug / Controls
	if (dbg.enableSettings) {
		ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(435.0f, 450.0f), ImGuiCond_Always);
		ImGui::Begin("Debug");

		// SHADOWS
		if (ImGui::CollapsingHeader("Shadow settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			bool shadows = dbg.enableShadows != 0;
			bool cascadeVP = dbg.enableCascadeVPs != 0;
			bool cascadeSplitView = dbg.showCascadeSplits != 0;

			if (ImGui::Checkbox("Enable Shadows##rt", &shadows))    dbg.enableShadows = shadows ? 1u : 0u;
			if (ImGui::Checkbox("Draw CascadeVPs##rt", &cascadeVP)) dbg.enableCascadeVPs = cascadeVP ? 1u : 0u;
			if (ImGui::Checkbox("Show Cascade splits##rt", &cascadeSplitView)) dbg.showCascadeSplits = cascadeSplitView ? 1u : 0u;

			auto& shadowControl = RenderScene::_shadowControl;
			//ImGui::SliderFloat("Split lamba##rt", &shadowControl.splitLambda, 0.0f, 1.0f);
			ImGui::SliderFloat("Bias##rt", &shadowControl.bias, 0.0000f, 0.0100f, "%.4f");
			//ImGui::SliderFloat("Frustum plane scale##rt", &shadowControl.lightDist, 0.0f, 5.0f);

		//	if (ImGui::BeginChild("ShadowMapBox", ImVec2(0, 270), true, ImGuiWindowFlags_NoScrollbar)) {
		//		const float previewSize = 125.0f;
		//		int imagesPerRow = 2;

		//		for (uint32_t i = 0; i < MAX_SHADOW_CASCADES; ++i) {
		//			if (i > 0 && (i % imagesPerRow) != 0) {
		//				ImGui::SameLine();
		//			}
		//			ImGui::Image(
		//				(ImTextureID)ResourceManager::getShadowMapDescriptors()[i],
		//				ImVec2(previewSize, previewSize)
		//			);
		//		}
		//	}
		//	ImGui::EndChild();
		}

		// Environment images
		static int selectedEnv = 0;

		if (ImGui::CollapsingHeader("Environment Maps##settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (ImGui::BeginCombo("Active Environment", fmt::format("Image {}", selectedEnv + 1).c_str())) {
				for (auto& env : ResourceManager::_environmentSets) {
					if (env.setIndex == UINT32_MAX) break;

					bool isSelected = (selectedEnv == static_cast<int>(env.setIndex));
					std::string label = fmt::format("Image {}", env.setIndex + 1);

					if (ImGui::Selectable(label.c_str(), isSelected)) {
						selectedEnv = env.setIndex;
						dbg.activeEnvMap = env.setIndex;
					}

					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}

		// Lighting
		if (ImGui::CollapsingHeader("Lighting##settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			auto& scene = RenderScene::getCurrentSceneData();
			static glm::vec3 sunCol = glm::vec3(scene.sunlightColor);
			static float     sunI = scene.sunlightColor.w;
			static glm::vec3 sunDir = glm::vec3(scene.sunlightDirection);

			ImGui::SliderFloat3("Sun Dir##light", glm::value_ptr(sunDir), -1.0f, 1.0f);
			ImGui::SliderFloat3("Sun Color##light", glm::value_ptr(sunCol), 0.0f, 1.0f);
			ImGui::SliderFloat("Sun Intensity##light", &sunI, 0.0f, 5.0f);

			scene.sunlightColor = glm::vec4(sunCol, sunI);
			scene.sunlightDirection = glm::vec4(sunDir, 0.0f);
		}

		// Volumetrics
		if (ImGui::CollapsingHeader("Volumetrics##settings"))
		{
			bool volumetrics = dbg.enableVolumetrics != 0;
			auto& volSettings = profiler.volLightSettings;

			if (ImGui::Checkbox("Enable Volumetrics##vol", &volumetrics))
			{
				dbg.enableVolumetrics = volumetrics ? 1u : 0u;
			}

			ImGui::Separator();
			ImGui::TextUnformatted("Fog");
			ImGui::Separator();

			ImGui::SliderFloat(
				"Density##vol",
				&volSettings.density,
				0.0f,
				0.25f,
				"%.3f"
			);

			ImGui::SliderFloat(
				"Scattering##vol",
				&volSettings.scatteringStrength,
				0.0f,
				15.0f
			);

			ImGui::SliderFloat(
				"Extinction##vol",
				&volSettings.extinction,
				0.0f,
				1.0f
			);

			ImGui::SliderFloat(
				"Asymmetry Factor##vol",
				&volSettings.asymmetryFactor,
				0.0f,
				0.95f,
				"%.2f"
			);

			ImGui::SliderFloat(
				"Height Falloff##vol",
				&volSettings.heightFalloff,
				0.0f,
				0.1f,
				"%.2f"
			);

			ImGui::Separator();
			ImGui::TextUnformatted("Ray March");
			ImGui::Separator();

			ImGui::SliderFloat(
				"Max Distance##vol",
				&volSettings.maxDistance,
				10.0f,
				150.0f
			);

			ImGui::SliderFloat(
				"Min Transmittance##vol",
				&volSettings.minTransmittance,
				0.9f,
				1.0f,
				"%.2f"
			);

			ImGui::SliderInt(
				"Beam Power##vol",
				&volSettings.beamPower,
				2,
				6
			);

			ImGui::SliderFloat(
				"Jitter Strength##vol",
				&volSettings.jitterStrength,
				0.0f,
				1.0f
			);

			ImGui::SliderInt(
				"Step Count##vol",
				&volSettings.stepCount,
				8,
				128
			);

			ImGui::Separator();
			ImGui::TextUnformatted("Blur");
			ImGui::Separator();

			ImGui::SliderFloat(
				"Blur Radius##vol",
				&volSettings.blurRadius,
				1.0f,
				10.0f
			);

			ImGui::SliderFloat(
				"Blur Depth Sigma##vol",
				&volSettings.blurDepthSigma,
				0.0f,
				5.0f
			);

			ImGui::SliderFloat(
				"Blur Weight Sigma##vol",
				&volSettings.blurWeightSigma,
				0.5f,
				5.0f
			);
		}

		// Ambient occlusion
		if (ImGui::CollapsingHeader("Ambient Occlusion##settings")) {
			const char* aoModes[] = { "Off", "SSAO", "GTAO" };
			int current = dbg.aoMode;

			if (ImGui::Combo("AO Method", &current, aoModes, IM_ARRAYSIZE(aoModes)))
				dbg.aoMode = static_cast<uint32_t>(current);

			if (dbg.aoMode == AO_SSAO)
			{
				auto& ssao = profiler.ssaoSettings;
				ImGui::SliderFloat("Radius##ssao", &ssao.aoRadius, 0.25f, 4.0f, "%.3f");
				ImGui::SliderFloat("Bias##ssao", &ssao.bias, 0.01f, 0.1f);
				ImGui::SliderFloat("Intensity##ssao", &ssao.intensity, 0.1f, 5.0f);
				ImGui::SliderInt("Blur Radius##ssao", &ssao.blurRadius, 1, 8);
				ImGui::SliderInt("Samples##ssao", (int*)&ssao.sampleCount, 8, KERNEL_BLOCK_SIZE);
			}

			if (dbg.aoMode == AO_GTAO)
			{
				auto& g = profiler.gtaoSettings;

				// AO shape
				ImGui::SliderFloat("Radius##gtao",
					&g.effectRadius, 0.02f, 0.30f, "%.3f");
				ImGui::SliderFloat("Falloff Range##gtao",
					&g.effectFalloffRange, 0.30f, 1.0f, "%.2f");
				ImGui::SliderFloat("Distribution##gtao",
					&g.sampleDistributionPower, 1.0f, 4.0f, "%.2f");

				// thickness
				ImGui::SliderFloat("Thin Occluder Comp##gtao",
					&g.thinOccluderCompensation, 0.0f, 1.0f, "%.2f");

				// sampling quality
				ImGui::SliderInt("Slice Count##gtao",
					reinterpret_cast<int*>(&g.sliceCount), 4, 12);
				ImGui::SliderInt("Steps Per Slice##gtao",
					reinterpret_cast<int*>(&g.stepsPerSliceCount), 2, 8);

				// filter
				ImGui::SliderFloat("Filter Sharpness##gtao",
					&g.sharpness, 0.5f, 5.0f);
				ImGui::SliderFloat("Filter Radius##gtao",
					&g.radius, 1.0f, 6.0f);
			}

		}

		// Pipeline override
		if (ImGui::CollapsingHeader("Pipeline Views##ovr", 0)) {
			bool obb = dbg.enableOBBs != 0;
			if (ImGui::Checkbox("Enable OBB##rt", &obb))    dbg.enableOBBs = obb ? 1u : 0u;

			ImGui::Checkbox("Pipeline Override##ovr", &profiler.pipeOverride.enabled);
			auto swappables = Pipelines::getSwappablePipelines();
			static int selected = 0;

			std::vector<const char*> names; names.reserve(swappables.size());
			for (auto& [id, handle] : swappables) names.push_back(handle.name.c_str());

			if (!names.empty() && ImGui::Combo("Force Pipeline##ovr", &selected, names.data(), (int)names.size())) {
				profiler.pipeOverride.selectedID = swappables[selected].first;
			}
		}

		// Fragment debug overlays
		if (ImGui::CollapsingHeader("Shading Overlay##overlay", ImGuiTreeNodeFlags_DefaultOpen)) {
			enum Overlay {
				O_Complete, O_Normals, O_Albedo, O_Emissive, O_BakedAO, O_AO,
				O_Specular, O_Diffuse, O_Metallic, O_Roughness
			};

			auto pickFromToggles = [&] {
				if (dbg.showNormals)          return O_Normals;
				if (dbg.showAlbedo)           return O_Albedo;
				if (dbg.showEmissive)         return O_Emissive;
				if (dbg.showBakedAO)          return O_BakedAO;
				if (dbg.showAmbientOcclusion) return O_AO;
				if (dbg.showSpecular)         return O_Specular;
				if (dbg.showDiffuse)          return O_Diffuse;
				if (dbg.showMetallic)         return O_Metallic;
				if (dbg.showRoughness)        return O_Roughness;
				return O_Complete;
				};

			auto applyOverlay = [&](int o) {
				dbg.showNormals = dbg.showAlbedo = dbg.showEmissive = 0;
				dbg.showBakedAO = dbg.showAmbientOcclusion = dbg.showSpecular = dbg.showDiffuse = 0;
				dbg.showMetallic = dbg.showRoughness = dbg.showCascadeSplits = 0;
				switch (o) {
				case O_Normals:   dbg.showNormals = 1; break;
				case O_Albedo:    dbg.showAlbedo = 1; break;
				case O_Emissive:  dbg.showEmissive = 1; break;
				case O_BakedAO:   dbg.showBakedAO = 1; break;
				case O_AO:        dbg.showAmbientOcclusion = 1; break;
				case O_Specular:  dbg.showSpecular = 1; break;
				case O_Diffuse:   dbg.showDiffuse = 1; break;
				case O_Metallic:  dbg.showMetallic = 1; break;
				case O_Roughness: dbg.showRoughness = 1; break;
				default: break;
				}
			};

			static int overlay = pickFromToggles();

			// Row 1
			if (ImGui::RadioButton("Complete##ov", &overlay, O_Complete))
				applyOverlay(overlay);
			ImGui::SameLine();
			if (ImGui::RadioButton("Albedo##ov", &overlay, O_Albedo))
				applyOverlay(overlay);
			ImGui::SameLine();
			if (ImGui::RadioButton("Normals##ov", &overlay, O_Normals))
				applyOverlay(overlay);
			ImGui::SameLine();
			if (ImGui::RadioButton("Roughness##ov", &overlay, O_Roughness))
				applyOverlay(overlay);

			ImGui::NewLine();

			// Row 2
			if (ImGui::RadioButton("Metallic##ov", &overlay, O_Metallic))
				applyOverlay(overlay);
			ImGui::SameLine();
			if (ImGui::RadioButton("Ambient Occlusion##ov", &overlay, O_AO))
				applyOverlay(overlay);
			ImGui::SameLine();
			if (ImGui::RadioButton("Specular##ov", &overlay, O_Specular))
				applyOverlay(overlay);
			ImGui::SameLine();
			if (ImGui::RadioButton("Diffuse##ov", &overlay, O_Diffuse))
				applyOverlay(overlay);

			ImGui::NewLine();

			// Row 3
			ImGui::SameLine();
			if (ImGui::RadioButton("Emissive##ov", &overlay, O_Emissive))
				applyOverlay(overlay);
			ImGui::SameLine();
			if (ImGui::RadioButton("Baked AO##ov", &overlay, O_BakedAO))
				applyOverlay(overlay);
		}

		ImGui::End();
	}

	ImGui::Render();

	uploadDebugInlineIfDirty(profiler);
}


// draws into a swapchain image
void EditorImgui::drawImgui(VkCommandBuffer cmd, VkImageView targetImageView, const VkExtent2D swapExtent, bool shouldClear) {
	VkClearValue clearValue{};
	clearValue.color = { 0.0f, 0.0f, 0.0f, 1.0f }; // Black clear color

	VkRenderingAttachmentInfo colorAttachment{};
	colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAttachment.pNext = nullptr;

	colorAttachment.imageView = targetImageView;
	colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.loadOp = shouldClear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	if (shouldClear) {
		colorAttachment.clearValue = clearValue;
	}

	VkRenderingInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;
	renderingInfo.renderArea = { {0, 0}, { swapExtent.width, swapExtent.height } };
	renderingInfo.layerCount = 1;
	renderingInfo.viewMask = 0;

	vkCmdBeginRendering(cmd, &renderingInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRendering(cmd);
}
#include "pch.h"

#include "EditorImgui.h"
#include "renderer/scene/RenderScene.h"
#include "engine/platform/input/UserInput.h"
#include "renderer/backend/Backend.h"

static void MyWindowFocusCallback(GLFWwindow* window, int focused) {
	ImGui_ImplGlfw_WindowFocusCallback(window, focused); // Forward to ImGui
	Engine::getProfiler().resetRenderTimers();
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
	pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
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
	init_info.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &Backend::getSwapchainDef().imageFormat;

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
			ImGui::Text("FPS: %.1f", stats.fps.load());
			ImGui::Text("Frame: %.3f ms", stats.frameTime.load());
			ImGui::Text("Draw:  %.3f ms", stats.drawTime.load());
			ImGui::Text("Scene: %.3f ms", stats.sceneUpdateTime.load());
			ImGui::Text("Triangles: %llu", (unsigned long long)stats.triangleCount.load());
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
				stats.opaqueIndirect.commands.load() + stats.transparentIndirect.commands.load();

			if (ImGui::BeginTable("DrawPathsTop", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn("API"); ImGui::TableSetupColumn("Commands"); ImGui::TableHeadersRow();
				ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("vkCmdDraw");
				ImGui::TableSetColumnIndex(1); ImGui::Text("%u", stats.directDraws.load());
				ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("vkCmdDrawIndirect");
				ImGui::TableSetColumnIndex(1); ImGui::Text("%u", indirectTotal);
				ImGui::EndTable();
			}

			if (ImGui::BeginTable("IndirectBreakdown", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn("Pass"); ImGui::TableSetupColumn("Commands"); ImGui::TableSetupColumn("Subdraws"); ImGui::TableHeadersRow();
				ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Opaque");
				ImGui::TableSetColumnIndex(1); ImGui::Text("%u", stats.opaqueIndirect.commands.load());
				ImGui::TableSetColumnIndex(2); ImGui::Text("%u", stats.opaqueIndirect.subdraws.load());
				ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Transparent");
				ImGui::TableSetColumnIndex(1); ImGui::Text("%u", stats.transparentIndirect.commands.load());
				ImGui::TableSetColumnIndex(2); ImGui::Text("%u", stats.transparentIndirect.subdraws.load());
				ImGui::EndTable();
			}
			ImGui::End();
		}
	}

	// Debug / Controls
	if (dbg.enableSettings) {
		ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(350.0f, 470.0f), ImGuiCond_Always);
		ImGui::Begin("Debug");

		// Render toggles
		if (ImGui::CollapsingHeader("Render Toggles", ImGuiTreeNodeFlags_DefaultOpen)) {
			bool obb = dbg.enableOBBs != 0;
			bool ssao = dbg.enableSSAO != 0;
			bool shadows = dbg.enableShadows != 0;
			bool tonemap = dbg.enableTonemap != 0;

			if (ImGui::Checkbox("Draw OBBs##rt", &obb))          dbg.enableOBBs = obb ? 1u : 0u;
			if (ImGui::Checkbox("Enable SSAO##rt", &ssao))       dbg.enableSSAO = ssao ? 1u : 0u;
			if (ImGui::Checkbox("Enable Shadows##rt", &shadows)) dbg.enableShadows = shadows ? 1u : 0u;
			if (ImGui::Checkbox("Enable Tonemap##rt", &tonemap)) dbg.enableTonemap = tonemap ? 1u : 0u;
		}

		// Pipeline override
		if (ImGui::CollapsingHeader("Pipeline##ovr", 0)) {
			ImGui::Checkbox("Pipeline Override##ovr", &profiler.pipeOverride.enabled);
			auto swappables = Pipelines::getSwappablePipelines();
			static int selected = 0;

			std::vector<const char*> names; names.reserve(swappables.size());
			for (auto& [id, handle] : swappables) names.push_back(handle.name.c_str());

			if (!names.empty() && ImGui::Combo("Force Pipeline##ovr", &selected, names.data(), (int)names.size())) {
				profiler.pipeOverride.selectedID = swappables[selected].first;
			}
		}

		// SSAO
		if (ImGui::CollapsingHeader("SSAO##settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			auto& ssao = profiler.ssaoSettings;
			ImGui::SliderFloat("Radius##ssao", &ssao.aoRadius, 0.025f, 2.0f, "%.3f");
			ImGui::SliderFloat("Bias##ssao", &ssao.bias, 0.0f, 0.1f, "%.4f");
			ImGui::SliderFloat("Intensity##ssao", &ssao.intensity, 0.10f, 1.5f, "%.2f");
			ImGui::SliderInt("BlurRadius##ssao", &ssao.blurRadius, 1, 8);
			ImGui::SliderInt("Samples##ssao", (int*)&ssao.sampleCount, 8, ResourceManager::_kernelBlockSize);
		}

		// Tonemap
		if (ImGui::CollapsingHeader("Tonemap##settings", 0)) {
			auto& tm = ResourceManager::toneMappingData;
			ImGui::SliderFloat("Brightness##tm", &tm.brightness, 0.0f, 2.0f);
			ImGui::SliderFloat("Saturation##tm", &tm.saturation, 0.0f, 2.0f);
			ImGui::SliderFloat("Contrast##tm", &tm.contrast, 0.0f, 2.0f);
		}

		// Lighting
		if (ImGui::CollapsingHeader("Lighting##settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			auto& scene = RenderScene::getCurrentSceneData();
			static glm::vec3 sunCol = glm::vec3(scene.sunlightColor);
			static float     sunI = scene.sunlightColor.w;
			static glm::vec3 sunDir = glm::vec3(scene.sunlightDirection);

			ImGui::SliderFloat3("Sun Color##light", glm::value_ptr(sunCol), 0.0f, 1.0f);
			ImGui::SliderFloat("Sun Intensity##light", &sunI, 0.0f, 5.0f);
			ImGui::SliderFloat3("Sun Dir##light", glm::value_ptr(sunDir), -1.0f, 1.0f);

			scene.sunlightColor = glm::vec4(sunCol, sunI);
			scene.sunlightDirection = glm::normalize(glm::vec4(sunDir, 0.0f));
		}

		// Fragment debug overlays
		if (ImGui::CollapsingHeader("Shading Overlay##overlay", ImGuiTreeNodeFlags_DefaultOpen)) {
			enum Overlay {
				O_Complete, O_Normals, O_Albedo, O_Emissive, O_AO, O_SSAO,
				O_Specular, O_Diffuse, O_Metallic, O_Roughness, O_Cascades
			};

			auto pickFromToggles = [&] {
				if (dbg.showNormals)       return O_Normals;
				if (dbg.showAlbedo)        return O_Albedo;
				if (dbg.showEmissive)      return O_Emissive;
				if (dbg.showAO)            return O_AO;
				if (dbg.showSSAO)          return O_SSAO;
				if (dbg.showSpecular)      return O_Specular;
				if (dbg.showDiffuse)       return O_Diffuse;
				if (dbg.showMetallic)      return O_Metallic;
				if (dbg.showRoughness)     return O_Roughness;
				if (dbg.showCascadeSplits) return O_Cascades;
				return O_Complete;
				};

			auto applyOverlay = [&](int o) {
				dbg.showNormals = dbg.showAlbedo = dbg.showEmissive = 0;
				dbg.showAO = dbg.showSSAO = dbg.showSpecular = dbg.showDiffuse = 0;
				dbg.showMetallic = dbg.showRoughness = dbg.showCascadeSplits = 0;
				switch (o) {
				case O_Normals:   dbg.showNormals = 1; break;
				case O_Albedo:    dbg.showAlbedo = 1; break;
				case O_Emissive:  dbg.showEmissive = 1; break;
				case O_AO:        dbg.showAO = 1; break;
				case O_SSAO:      dbg.showSSAO = 1; break;
				case O_Specular:  dbg.showSpecular = 1; break;
				case O_Diffuse:   dbg.showDiffuse = 1; break;
				case O_Metallic:  dbg.showMetallic = 1; break;
				case O_Roughness: dbg.showRoughness = 1; break;
				case O_Cascades:  dbg.showCascadeSplits = 1; break;
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
			if (ImGui::RadioButton("SSAO##ov", &overlay, O_SSAO))
				applyOverlay(overlay);
			ImGui::SameLine();
			if (ImGui::RadioButton("Specular##ov", &overlay, O_Specular))
				applyOverlay(overlay);
			ImGui::SameLine();
			if (ImGui::RadioButton("Diffuse##ov", &overlay, O_Diffuse))
				applyOverlay(overlay);

			ImGui::NewLine();

			// Row 3
			if (ImGui::RadioButton("Cascade Splits##ov", &overlay, O_Cascades))
				applyOverlay(overlay);
			ImGui::SameLine();
			if (ImGui::RadioButton("Emissive##ov", &overlay, O_Emissive))
				applyOverlay(overlay);
			ImGui::SameLine();
			if (ImGui::RadioButton("AO##ov", &overlay, O_AO))
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
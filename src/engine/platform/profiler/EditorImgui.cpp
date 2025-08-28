#include "pch.h"

#include "EditorImgui.h"
#include "renderer/scene/RenderScene.h"
#include "engine/platform/input/UserInput.h"

static void MyWindowFocusCallback(GLFWwindow* window, int focused) {
	ImGui_ImplGlfw_WindowFocusCallback(window, focused); // Forward to ImGui
	Engine::getProfiler().resetRenderTimers();
}

void EditorImgui::initImgui(
	const VkDevice device,
	const VkPhysicalDevice pDevice,
	const VkQueue gQueue,
	const VkInstance instance,
	const VkFormat swapFormat,
	DeletionQueue& queue)
{
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
	init_info.Instance = instance;
	init_info.PhysicalDevice = pDevice;
	init_info.Device = device;
	init_info.Queue = gQueue;
	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.UseDynamicRendering = true;

	//dynamic rendering parameters for imgui to use
	init_info.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapFormat;

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

// call before RenderFrame
void EditorImgui::renderImgui(Profiler& profiler) {

	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	auto& stats = profiler.getStats();
	const auto& debug = profiler.debugToggles;

	if (debug.enableStats) {
		const auto camera = RenderScene::getCamera();
		ImGui::SetNextWindowPos(
			ImVec2(ImGui::GetIO().DisplaySize.x - 10.f, 10.f),
			ImGuiCond_Always, ImVec2(1.0f, 0.0f));

		if (ImGui::Begin("Stats", nullptr,
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_AlwaysAutoResize))
		{
			// Top-line metrics
			ImGui::Text("Camera: %.2f %.2f %.2f",
				camera._position.x, camera._position.y, camera._position.z);
			ImGui::Text("FPS: %.1f", stats.fps.load());
			ImGui::Text("Frame: %.3f ms", stats.frameTime.load());
			ImGui::Text("Draw:  %.3f ms", stats.drawTime.load());
			ImGui::Text("Scene: %.3f ms", stats.sceneUpdateTime.load());
			ImGui::Text("Triangles: %llu",
				(unsigned long long)stats.triangleCount.load());
			ImGui::Text("VRAM: %llu MB",
				(unsigned long long)(stats.vramUsed.load() / (1024ull * 1024ull)));

			ImGui::Separator();
			ImGui::TextUnformatted("Draws");

			// only API call counts
			const uint32_t indirectCmdsTotal =
				stats.opaqueIndirect.commands.load() +
				stats.transparentIndirect.commands.load();

			if (ImGui::BeginTable("DrawPathsTop", 2,
				ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
			{
				ImGui::TableSetupColumn("API Calls");
				ImGui::TableSetupColumn("Commands");
				ImGui::TableHeadersRow();

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("vkCmdDraw");
				ImGui::TableSetColumnIndex(1); ImGui::Text("%u", stats.directDraws.load());

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("vkCmdDrawIndirect");
				ImGui::TableSetColumnIndex(1); ImGui::Text("%u", indirectCmdsTotal);

				ImGui::EndTable();
			}

			if (ImGui::BeginTable("IndirectBreakdown", 3,
				ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
			{
				ImGui::TableSetupColumn("Pass");
				ImGui::TableSetupColumn("Commands");
				ImGui::TableSetupColumn("Subdraws");
				ImGui::TableHeadersRow();

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Opaque");
				ImGui::TableSetColumnIndex(1); ImGui::Text("%u", stats.opaqueIndirect.commands.load());
				ImGui::TableSetColumnIndex(2); ImGui::Text("%u", stats.opaqueIndirect.subdraws.load());

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Transparent");
				ImGui::TableSetColumnIndex(1); ImGui::Text("%u", stats.transparentIndirect.commands.load());
				ImGui::TableSetColumnIndex(2); ImGui::Text("%u", stats.transparentIndirect.subdraws.load());

				ImGui::EndTable();
			}

			ImGui::End();
		}
	}


	if (debug.enableSettings) {
		ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(300.f, 330.f), ImGuiCond_Always);

		// -- DEBUG TOOLS WINDOW --
		ImGui::Begin("Debug");

		// Pipeline override section
		if (ImGui::CollapsingHeader("Pipeline Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Checkbox("Pipeline Override", &profiler.pipeOverride.enabled);

			auto swappables = Pipelines::getSwappablePipelines();

			std::vector<const char*> names;
			names.reserve(swappables.size());
			for (auto& [id, handle] : swappables) {
				names.push_back(handle.name.c_str());
			}

			static int selected = 0;
			if (ImGui::Combo("Force Pipeline", &selected, names.data(), static_cast<int>(names.size()))) {
				profiler.pipeOverride.selectedID = swappables[selected].first;
			}

			if (ImGui::TreeNode("Debug Draw")) {
				ImGui::Checkbox("Draw OBB", &profiler.debugToggles.showOBBs);
				ImGui::TreePop();
			}
		}

		// "tone map", not a very good one
		if (ImGui::CollapsingHeader("Options", ImGuiTreeNodeFlags_DefaultOpen)) {
			auto& color = ResourceManager::toneMappingData;
			ImGui::Text("Tone map color correction");
			ImGui::SliderFloat("Brightness", &color.brightness, 0.0f, 2.0f);
			ImGui::SliderFloat("Saturation", &color.saturation, 0.0f, 2.0f);
			ImGui::SliderFloat("Contrast", &color.contrast, 0.0f, 2.0f);
		}

		if (ImGui::CollapsingHeader("Scene Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
			auto& sceneData = RenderScene::getCurrentSceneData();
			static glm::vec3 ambientColor = glm::vec3(sceneData.ambientColor);
			static glm::vec3 sunlightColor = glm::vec3(sceneData.sunlightColor);
			static float lightIntensity = sceneData.sunlightColor.w;
			static glm::vec3 lightDir = glm::vec3(sceneData.sunlightDirection);

			if (ImGui::TreeNode("Light Colors")) {
				ImGui::SliderFloat3("Ambient Color", glm::value_ptr(ambientColor), 0.0f, 1.0f);
				ImGui::SliderFloat3("Sunlight Color", glm::value_ptr(sunlightColor), 0.0f, 1.0f);
				ImGui::TreePop();
			}

			ImGui::SliderFloat("Sunlight Intensity", &lightIntensity, 0.0f, 5.0f);
			ImGui::SliderFloat3("Light Direction", glm::value_ptr(lightDir), -1.0f, 1.0f);

			// Update actual scene data
			sceneData.ambientColor = glm::vec4(ambientColor, 1.0f);
			sceneData.sunlightColor = glm::vec4(sunlightColor, lightIntensity);
			sceneData.sunlightDirection = glm::normalize(glm::vec4(lightDir, 0.0f));
		}

		ImGui::End();
	}

	ImGui::Render();
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
#include "pch.h"

#include "EditorImgui.h"

#include "vulkan/Backend.h"
#include "renderer/gpu_types/PipelineManager.h"
#include "renderer/Renderer.h"
#include "input/UserInput.h"
#include "renderer/RenderScene.h"

static void MyWindowFocusCallback(GLFWwindow* window, int focused) {
	ImGui_ImplGlfw_WindowFocusCallback(window, focused); // Forward to ImGui
	Engine::getProfiler().resetRenderTimers();
}

void EditorImgui::initImgui(DeletionQueue& queue) {
	//  the size of the pool is very oversize, but it's copied from imgui demo
	//  itself.
	auto device = Backend::getDevice();

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
	init_info.Instance = Backend::getInstance();
	init_info.PhysicalDevice = Backend::getPhysicalDevice();
	init_info.Device = device;
	init_info.Queue = Backend::getGraphicsQueue().queue;
	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.UseDynamicRendering = true;

	auto format = Backend::getSwapchainDef().imageFormat;
	//dynamic rendering parameters for imgui to use
	init_info.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &format;

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
		ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10.f, 10.f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
		//	ImGui::SetNextWindowBgAlpha(1.0f);
		ImGui::Begin("Stats", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
		ImGui::Text("Camera Pos: %.2f %.2f %.2f", RenderScene::_mainCamera._position.x, RenderScene::_mainCamera._position.y, RenderScene::_mainCamera._position.z);
		ImGui::Text("FPS: %.1f", stats.fps.load());
		ImGui::Text("Frame Time: %f ms", stats.frameTime.load());
		ImGui::Text("Draw Time: %f ms", stats.drawTime.load());
		ImGui::Text("Scene Update Time: %f ms", stats.sceneUpdateTime.load());
		ImGui::Text("Triangles: %i", stats.triangleCount.load());
		ImGui::Text("Draws: %i", stats.drawCalls.load());
		ImGui::Text("VRAM Used: %llu MB", stats.vramUsed.load() / (1024ull * 1024ull));
		ImGui::End();
	}

	if (debug.enableSettings) {
		ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(300.f, 330.f), ImGuiCond_Always);

		// -- DEBUG TOOLS WINDOW --
		ImGui::Begin("Debug");

		// Pipeline override section
		if (ImGui::CollapsingHeader("Pipeline Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Checkbox("Pipeline Override", &profiler.pipeOverride.enabled);

			static int selected = static_cast<int>(profiler.pipeOverride.selected);
			const char* names[] = { "Opaque", "Transparent", "Wireframe" };

			if (ImGui::Combo("Force Pipeline", &selected, names, IM_ARRAYSIZE(names))) {
				profiler.pipeOverride.selected = static_cast<PipelineType>(selected);
			}

			if (ImGui::TreeNode("Debug Draw")) {
				ImGui::Checkbox("Draw AABB", &profiler.debugToggles.showAABBs);
				ImGui::TreePop();
			}
		}

		// Background controls section (Compute shader/post process effects)
		if (ImGui::CollapsingHeader("Options", ImGuiTreeNodeFlags_DefaultOpen)) {
			auto& color = ResourceManager::toneMappingData;
			ImGui::Text("Post process color correction");
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
void EditorImgui::drawImgui(VkCommandBuffer cmd, VkImageView targetImageView, bool shouldClear) {
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

	auto extent = Backend::getSwapchainDef().extent;

	VkRenderingInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;
	renderingInfo.renderArea = { {0, 0}, { extent.width, extent.height } };
	renderingInfo.layerCount = 1;
	renderingInfo.viewMask = 0;

	vkCmdBeginRendering(cmd, &renderingInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRendering(cmd);
}
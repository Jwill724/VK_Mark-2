#include "pch.h"

#include "EditorImgui.h"

#include "vulkan/Backend.h"
#include "vulkan/PipelineManager.h"
#include "renderer/Renderer.h"

void EditorImgui::initImgui() {
	//  the size of the pool is very oversize, but it's copied from imgui demo
	//  itself.

	VkDevice device = Backend::getDevice();

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

	ImGui_ImplGlfw_InitForVulkan(Engine::getWindow(), true);

	// this initializes imgui for Vulkan
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = Backend::getInstance();
	init_info.PhysicalDevice = Backend::getPhysicalDevice();
	init_info.Device = device;
	init_info.Queue = Backend::getGraphicsQueue();
	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.UseDynamicRendering = true;

	//dynamic rendering parameters for imgui to use
	init_info.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &Backend::getSwapchainImageFormat();

	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info);

	ImGui_ImplVulkan_CreateFontsTexture();

	// add the destroy the imgui created structures
	Engine::getDeletionQueue().push_function([=]() {
		ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(Backend::getDevice(), imguiPool, nullptr);
	});
}

// call at start of renderFrame
void EditorImgui::renderImgui() {

	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	//some imgui UI to test
//	ImGui::ShowDemoWindow();
	//make imgui calculate internal draw structures

	if (ImGui::Begin("background")) {
		ImGui::SliderFloat("Render Scale", &Renderer::getRenderScale(), 0.3f, 1.f);

		PipelineEffect& selected = Pipelines::drawImagePipeline.getBackgroundEffects();

		ImGui::Text("Selected effect: ", selected.name);

		ImGui::SliderInt("Effect Index", &Pipelines::drawImagePipeline.getCurrentBackgroundEffect(), 0, static_cast<int>(Pipelines::drawImagePipeline.backgroundEffects.size()) - 1);

		ImGui::InputFloat4("data1", (float*)&selected.data.data1);
		ImGui::InputFloat4("data2", (float*)&selected.data.data2);
		ImGui::InputFloat4("data3", (float*)&selected.data.data3);
		ImGui::InputFloat4("data4", (float*)&selected.data.data4);
	}
	ImGui::End();

	ImGui::Render();
}

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

	VkRenderingInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;
	renderingInfo.renderArea = { {0, 0}, { Backend::getSwapchainExtent().width, Backend::getSwapchainExtent().height } };
	renderingInfo.layerCount = 1;
	renderingInfo.viewMask = 0;

	vkCmdBeginRendering(cmd, &renderingInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRendering(cmd);
}

void EditorImgui::MyWindowFocusCallback(GLFWwindow* window, int focused) {
	ImGui_ImplGlfw_WindowFocusCallback(window, focused);  // Forward to ImGui
}
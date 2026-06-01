#include "pch.h"

#include "EditorImgui.h"

#include "input/Camera.h"
#include "renderer/scene/World.h"
#include "renderer/scene/Scene.h"
#include "renderer/Renderer.h"
#include "renderer/backend/Device.h"
#include "renderer/backend/Swapchain.h"
#include "renderer/backend/Queue.h"

#include "../core/Environment.h"

#include "renderer/scene/LightingSystem.h"

#include "renderer/RendererDefinitions.h"

// TODO: Refactor this fucking mess

namespace RD = RendererDefinitions;

static void MyWindowFocusCallback(GLFWwindow* window, int focused)
{
	ImGui_ImplGlfw_WindowFocusCallback(window, focused);
}

namespace
{
	struct UIContext
	{
		Profiler* profiler = nullptr;
		FrameStats* stats = nullptr;
		RD::RenderToggles* dbg = nullptr;
	};

	namespace UI
	{
		struct WindowScope
		{
			bool isOpen = false;

			WindowScope(
				const char* title,
				bool* pOpen,
				ImGuiWindowFlags flags)
			{
				isOpen = ImGui::Begin(title, pOpen, flags);
			}

			~WindowScope()
			{
				ImGui::End();
			}

			explicit operator bool() const
			{
				return isOpen;
			}
		};

		struct TableScope
		{
			bool isOpen = false;

			TableScope(
				const char* id,
				int columnCount,
				ImGuiTableFlags flags)
			{
				isOpen = ImGui::BeginTable(id, columnCount, flags);
			}

			~TableScope()
			{
				if (isOpen) {
					ImGui::EndTable();
				}
			}

			explicit operator bool() const
			{
				return isOpen;
			}
		};

		struct IdScope
		{
			IdScope(const char* stringId)
			{
				ImGui::PushID(stringId);
			}

			IdScope(int intId)
			{
				ImGui::PushID(intId);
			}

			~IdScope()
			{
				ImGui::PopID();
			}
		};

		struct StyleVarScope
		{
			bool pushed = false;

			StyleVarScope(ImGuiStyleVar var, float v)
			{
				ImGui::PushStyleVar(var, v);
				pushed = true;
			}

			StyleVarScope(ImGuiStyleVar var, const ImVec2& v)
			{
				ImGui::PushStyleVar(var, v);
				pushed = true;
			}

			~StyleVarScope()
			{
				if (pushed) {
					ImGui::PopStyleVar();
				}
			}
		};

		static void separatorText(const char* text)
		{
#if IMGUI_VERSION_NUM >= 18923
			ImGui::SeparatorText(text);
#else
			ImGui::Separator();
			ImGui::TextUnformatted(text);
			ImGui::Separator();
#endif
		}
	}

	namespace UIWidgets
	{
		static bool toggleU32(const char* label, uint32_t* valueU32)
		{
			bool valueBool = (*valueU32 != 0u);
			bool changed = ImGui::Checkbox(label, &valueBool);
			if (changed) {
				*valueU32 = valueBool ? 1u : 0u;
			}
			return changed;
		}

		static bool sliderU32(
			const char* label,
			uint32_t* valueU32,
			uint32_t minValue,
			uint32_t maxValue)
		{
			int valueInt = static_cast<int>(*valueU32);
			const int minInt = static_cast<int>(minValue);
			const int maxInt = static_cast<int>(maxValue);

			bool changed = ImGui::SliderInt(label, &valueInt, minInt, maxInt);
			if (!changed) {
				return false;
			}

			if (valueInt < minInt) valueInt = minInt;
			if (valueInt > maxInt) valueInt = maxInt;
			*valueU32 = static_cast<uint32_t>(valueInt);
			return true;
		}
	}

	using PanelFn = void(*)(UIContext& ui);

	struct Panel
	{
		const char* name = "";
		PanelFn fn = nullptr;
	};

	struct PanelRegistry
	{
		std::vector<Panel> panels;

		void addPanel(const char* name, PanelFn fn)
		{
			Panel panel;
			panel.name = name;
			panel.fn = fn;
			panels.push_back(panel);
		}

		void draw(UIContext& ui)
		{
			for (const Panel& panel : panels) {
				if (!panel.fn) continue;
				panel.fn(ui);
			}
		}
	};


	static void DrawCategorySelector(Editor::SettingsCategory& selectedCategory)
	{
		const char* categoryLabels[] = {
			"Render",
			"Lighting",
			"Post FX",
			"Pipelines"
		};

		static_assert(
			IM_ARRAYSIZE(categoryLabels) == static_cast<int>(Editor::SettingsCategory::Count),
			"Category label count mismatch.");

		UI::separatorText("Categories");

		for (int categoryIndex = 0; categoryIndex < static_cast<int>(Editor::SettingsCategory::Count); ++categoryIndex) {
			const bool isSelected = (static_cast<int>(selectedCategory) == categoryIndex);

			if (ImGui::Selectable(categoryLabels[categoryIndex], isSelected)) {
				selectedCategory = (Editor::SettingsCategory)categoryIndex;
			}
		}
	}

	static void DrawCategoryRender(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;
		Profiler& profiler = *ui.profiler;
		FrameStats& stats = *ui.stats;
		{
			UI::separatorText("Camera");
			auto& camera = World::GetScene().GetCamera();

			const auto& pos = camera.GetPosition();
			ImGui::Text("World Position: %.2f %.2f %.2f", pos.x, pos.y, pos.z);
			const auto& camVelo = camera.GetVelocity();
			ImGui::Text("Velocity: %.2f %.2f %.2f", camVelo.x, camVelo.y, camVelo.z);

			if (ImGui::CollapsingHeader("Camera Settings"))
			{
				float camSens = camera.GetSensitivity();
				ImGui::SliderFloat("Sensitivity", &camSens, 1.0, 100.0, "%.0f");
				camera.SetSensitivity(camSens);

				float camFOV = camera.GetFovY();
				ImGui::SliderFloat("FOV", &camFOV, Camera::CAMERA_MIN_FOV, Camera::CAMERA_MAX_FOV, "%.0f");
				camera.SetFovY(camFOV);

				float maxSpeed = camera.GetMaxSpeed();
				ImGui::SliderFloat("Max Speed", &maxSpeed, 1.0, 100.0, "%.0f");
				camera.SetMaxSpeed(maxSpeed);

				float minSpeed = camera.GetMinSpeed();
				ImGui::SliderFloat("Min Speed", &minSpeed,  1.0, 100.0, "%.0f");
				camera.SetMinSpeed(minSpeed);

				float accel = camera.GetAcceleration();
				ImGui::SliderFloat("Acceleration", &accel, 1.0, 100.0, "%.0f");
				camera.SetAcceleration(accel);

				float damping = camera.GetDamping();
				ImGui::SliderFloat("Damping", &damping, 1.0, 100.0, "%.0f");
				camera.SetDamping(damping);
			}
		}

		UI::separatorText("Shadows");

		bool shadows = dbg.enableShadows != 0u;
		bool contact = dbg.enableSSS != 0u;
		bool cascadeSplitView = dbg.showCascadeSplits != 0u;

		if (ImGui::Checkbox("Enable Shadows##rt", &shadows)) {
			dbg.enableShadows = shadows ? 1u : 0u;
		}

		if (dbg.enableShadows) {
			if (ImGui::Checkbox("Enable Screen Space Contact Shadows##rt", &contact)) {
				dbg.enableSSS = contact ? 1u : 0u;
			}
			if (dbg.enableSSS) {
				auto& contactShadowSettings = profiler.contactShadowsSettings;
				ImGui::SliderFloat("Surface Thickness##rt", &contactShadowSettings.surfaceThickness, 0.001f, 0.05f, "%.3f");
				ImGui::SliderFloat("Bilinear Threshold##rt", &contactShadowSettings.bilinearThreshold, 0.001f, 0.1f, "%.3f");
				int shadowContrastInt = static_cast<int>(contactShadowSettings.shadowContrast);
				ImGui::SliderInt("Shadow Contrast##rt", &shadowContrastInt, 1, 8);
				contactShadowSettings.shadowContrast = static_cast<float>(shadowContrastInt);
			}

			if (ImGui::Checkbox("Show Cascade splits##rt", &cascadeSplitView)) {
				dbg.showCascadeSplits = cascadeSplitView ? 1u : 0u;
			}
		}

		UI::separatorText("Anti-Aliasing");

		const char* aaModes[] = { "Off", "CMAA2", "SMAA", "FXAA", "TAA (WIP)" };
		int currentAA = (int)dbg.aaMode;

		if (ImGui::Combo("AA Method", &currentAA, aaModes, IM_ARRAYSIZE(aaModes))) {
			dbg.aaMode = static_cast<uint32_t>(currentAA);
		}
		if (dbg.aaMode == static_cast<uint32_t>(RD::AntiAliasingMethod::AA_TAA)) {
			auto& taaSettings = profiler.taaSettings;
			ImGui::SliderFloat("Min Blend", &taaSettings.minBlend, 0.01f, 1.0f, "%.2f");
			ImGui::SliderFloat("Max Blend", &taaSettings.maxBlend, 0.01f, 1.0f, "%.2f");
			ImGui::SliderFloat("Depth Disocclusion Scale", &taaSettings.depthDisocclusionScale, 1.0f, 500.0f);
		}

		UI::separatorText("Environment");

		static int selectedEnv = 0;

		if (ImGui::BeginCombo("Active", fmt::format("Image {}", selectedEnv + 1).c_str())) {
			for (uint32_t i = 0; i < Environment::_HDRPathCount; ++i) {
				const bool isSelected = (selectedEnv == static_cast<int>(i));
				const std::string label = fmt::format("Image {}", i + 1);

				if (ImGui::Selectable(label.c_str(), isSelected)) {
					selectedEnv = static_cast<int>(i);
					dbg.activeEnvMap = i;
				}

				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		UI::separatorText("Transparency");
		auto oitScale = profiler.forwardPush.oitDepthScale;

		ImGui::SliderFloat("OIT Z Scale", &oitScale, 50.0f, 2000.0f, "%.0f");
		profiler.forwardPush.oitDepthScale = oitScale;

		UI::separatorText("Shading Overlay");

		enum Overlay
		{
			O_Complete,
			O_Normals,
			O_Albedo,
			O_Emissive,
			O_IrradianceNormal,
			O_AO,
			O_Specular,
			O_Diffuse,
			O_Metallic,
			O_Roughness,
			O_SSS
		};

		auto pickFromToggles = [&]() -> int
			{
				if (dbg.showNormals)            return O_Normals;
				if (dbg.showAlbedo)             return O_Albedo;
				if (dbg.showEmissive)           return O_Emissive;
				if (dbg.showBentNormals)        return O_IrradianceNormal;
				if (dbg.showAmbientOcclusion)   return O_AO;
				if (dbg.showSpecular)           return O_Specular;
				if (dbg.showDiffuse)            return O_Diffuse;
				if (dbg.showMetallic)           return O_Metallic;
				if (dbg.showRoughness)          return O_Roughness;
				if (dbg.showSSS)                return O_SSS;
				return O_Complete;
			};

		auto applyOverlay = [&](int overlay)
			{
				dbg.showNormals = 0;
				dbg.showAlbedo = 0;
				dbg.showEmissive = 0;
				dbg.showBentNormals = 0;
				dbg.showAmbientOcclusion = 0;
				dbg.showSpecular = 0;
				dbg.showDiffuse = 0;
				dbg.showMetallic = 0;
				dbg.showRoughness = 0;
				dbg.showSSS = 0;

				switch (overlay) {
				case O_Normals:              dbg.showNormals = 1; break;
				case O_Albedo:               dbg.showAlbedo = 1; break;
				case O_Emissive:             dbg.showEmissive = 1; break;
				case O_IrradianceNormal:     dbg.showBentNormals = 1; break;
				case O_AO:                   dbg.showAmbientOcclusion = 1; break;
				case O_Specular:             dbg.showSpecular = 1; break;
				case O_Diffuse:              dbg.showDiffuse = 1; break;
				case O_Metallic:             dbg.showMetallic = 1; break;
				case O_Roughness:            dbg.showRoughness = 1; break;
				case O_SSS:                  dbg.showSSS = 1; break;
				default: break;
				}
			};

		static int overlay = O_Complete;
		overlay = pickFromToggles();

		if (ImGui::RadioButton("Complete##ov", &overlay, O_Complete))  applyOverlay(overlay);
		ImGui::SameLine();
		if (ImGui::RadioButton("Albedo##ov", &overlay, O_Albedo))      applyOverlay(overlay);
		ImGui::SameLine();
		if (ImGui::RadioButton("Normals##ov", &overlay, O_Normals))    applyOverlay(overlay);
		ImGui::SameLine();
		if (ImGui::RadioButton("Roughness##ov", &overlay, O_Roughness)) applyOverlay(overlay);

		ImGui::NewLine();

		if (ImGui::RadioButton("Metallic##ov", &overlay, O_Metallic))  applyOverlay(overlay);
		ImGui::SameLine();
		if (ImGui::RadioButton("AO##ov", &overlay, O_AO)) applyOverlay(overlay);
		ImGui::SameLine();
		if (ImGui::RadioButton("Specular##ov", &overlay, O_Specular))  applyOverlay(overlay);
		ImGui::SameLine();
		if (ImGui::RadioButton("Diffuse##ov", &overlay, O_Diffuse))    applyOverlay(overlay);

		ImGui::NewLine();

		if (ImGui::RadioButton("Emissive##ov", &overlay, O_Emissive))   applyOverlay(overlay);
		ImGui::SameLine();
		if (ImGui::RadioButton("Bent Normals##ov", &overlay, O_IrradianceNormal)) applyOverlay(overlay);
		ImGui::SameLine();
		if (ImGui::RadioButton("Contact Shadows##ov", &overlay, O_SSS)) applyOverlay(overlay);
	}

	static void drawCategoryLighting(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;
		Profiler& profiler = *ui.profiler;

		UI::separatorText("Sun");

		auto& scene = World::GetScene().GetSceneData();

		static glm::vec3 sunCol = glm::vec3(scene.sunlightColor);
		static float sunI = scene.sunlightColor.w;
		static glm::vec3 sunDir = glm::vec3(scene.sunlightDirection);

		ImGui::SliderFloat3("Sun Dir##light", glm::value_ptr(sunDir), -0.5f, 0.5f);
		ImGui::SliderFloat3("Sun Color##light", glm::value_ptr(sunCol), 0.0f, 1.0f);

		ImGui::SliderFloat("Sun Intensity##light", &sunI, 2.5f, 20.0f);

		scene.sunlightColor = glm::vec4(sunCol, sunI);
		scene.sunlightDirection = glm::vec4(sunDir, 0.0f);

		ImGui::NewLine();
		UI::separatorText("Local Lights");

		bool dynamicLights = LightingSystem::_dynamicLightsEnabled;
		if (ImGui::Checkbox("Dynamic Lights##light", &dynamicLights)) {
			LightingSystem::_dynamicLightsEnabled = dynamicLights ? 1u : 0u;
		}

		static uint32_t targetLightCount = 0u;
		if (UIWidgets::sliderU32("Light Count##light", &targetLightCount, 0u, static_cast<uint32_t>(RD::MAX_VISIBLE_LIGHTS))) {
			LightingSystem::SetTargetActiveLightCount(targetLightCount);
		}

		const uint32_t activeCount = LightingSystem::GetActiveLightCount();
		ImGui::Text("Active: %u / %u", activeCount, static_cast<uint32_t>(RD::MAX_VISIBLE_LIGHTS));

		auto& flashlight = LightingSystem::_flashlightSettings;
		auto& flashlightReal = LightingSystem::_mainFlashLight;
		UI::separatorText("Flash Light settings");
		ImGui::SliderFloat("Lag Strength", &flashlightReal.m_lagStrength, 10.0, 100.0f);
		ImGui::SliderFloat("Sway Strength", &flashlightReal.m_swayStrength, 0.001f, 0.1f, "%.3f");
		//ImGui::SliderFloat("light radius##light", &flashlight.radius, 5, 100.0f);
		ImGui::SliderFloat("Intensity##light", &flashlight.intensity, 10.0f, 500.0f);
		//ImGui::SliderFloat("light outer degree##light", &flashlight.outerDeg, 10.0f, 40.0f);
		//ImGui::SliderFloat("light inner degree##light", &flashlight.innerDeg, 10.0f, 40.0f);
		//ImGui::SliderFloat("Offset R##light", &flashlight.offsetRight, -0.2f, 0.2f, "%.2f");
		//ImGui::SliderFloat("Offset D##light", &flashlight.offsetDown, -0.2f, 0.2f, "%.2f");
		//ImGui::SliderFloat("Offset L##light", &flashlight.offsetFwd, -0.2f, 0.2f, "%.2f");
		//ImGui::SliderFloat("fov y scale##light", &flashlight.fovYScale, 0.1f, 5.0f);

		//ImGui::SliderFloat("near projection##light", &flashlight.nearProj, 0.1f, 3.0f);
		//ImGui::SliderFloat("shadow bias##light", &flashlight.shadowBias, 0.0001f, 1.5f, "%.4f");
		//ImGui::SliderFloat("radius texels##light", &flashlight.radiusTexels, 1.0f, 4.0f);


		ImGui::NewLine();
		UI::separatorText("Screen Space Ambient Occlusion");

		const char* aoModes[] = { "Off", "VBAO", "VBAO + Bent Normals" };
		int currentAO = (int)dbg.aoMode;

		if (ImGui::Combo("AO Method", &currentAO, aoModes, IM_ARRAYSIZE(aoModes))) {
			dbg.aoMode = static_cast<uint32_t>(currentAO);
		}

		//auto& ssaoSettings = profiler.ssaoSettings;
		//if (dbg.aoMode != AO_OFF) {
		//	// Core ao settings
		//	//ImGui::SliderFloat("Radius##gtao", &ssaoSettings.effectRadius, 0.2f, 0.5f, "%.2f");
		//	//ImGui::SliderFloat("Falloff Range##gtao", &ssaoSettings.effectFalloffRange, 0.20f, 1.0f, "%.3f");
		//	//ImGui::SliderFloat("Filter Sharpness##gtao", &ssaoSettings.sharpness, 0.5f, 5.0f);
		//	//ImGui::SliderFloat("Filter Radius##gtao", &ssaoSettings.radius, 1.0f, 6.0f);
		//}

		ImGui::NewLine();
		UI::separatorText("Volumetrics");

		bool volumetrics = dbg.enableVolumetrics != 0u;
		auto& volSettings = profiler.volLightSettings;

		if (ImGui::Checkbox("Enable Volumetrics##vol", &volumetrics)) {
			dbg.enableVolumetrics = volumetrics ? 1u : 0u;
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Fog");
		ImGui::Separator();

		ImGui::SliderFloat("Density##vol", &volSettings.density, 0.0f, 0.1f, "%.3f");
		ImGui::SliderFloat("Scattering##vol", &volSettings.scatteringStrength, 0.0f, 100.0f);
		ImGui::SliderFloat("Extinction##vol", &volSettings.extinction, 0.0f, 1.0f);
		ImGui::SliderFloat("Asymmetry Factor##vol", &volSettings.asymmetryFactor, 0.0f, 0.95f, "%.2f");
		ImGui::SliderFloat("Height Falloff##vol", &volSettings.heightFalloff, 0.0f, 0.1f, "%.2f");

		ImGui::Separator();
		ImGui::TextUnformatted("Ray March");
		ImGui::Separator();

		ImGui::SliderFloat("Max Distance##vol", &volSettings.maxDistance, 10.0f, 250.0f);
		ImGui::SliderFloat("Min Transmittance##vol", &volSettings.minTransmittance, 0.9f, 1.0f, "%.2f");
		ImGui::SliderInt("Beam Power##vol", &volSettings.beamPower, 2, 6);
		ImGui::SliderFloat("Jitter Strength##vol", &volSettings.jitterStrength, 0.0f, 1.0f);

		ImGui::Separator();
		ImGui::TextUnformatted("Blur");
		ImGui::Separator();

		ImGui::SliderFloat("Blur Radius##vol", &volSettings.blurRadius, 1.0f, 4.0f, "%.0f");
		ImGui::SliderFloat("Blur Depth Sigma##vol", &volSettings.blurDepthSigma, 0.005f, 0.02f, "%.3f");
		ImGui::SliderFloat("Blur Weight Sigma##vol", &volSettings.blurWeightSigma, 0.5f, 2.0f, "%.2f");
	}

	static void drawCategoryPostFX(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;
		Profiler& profiler = *ui.profiler;

		{
			UI::separatorText("Tone Mapping");
			//const char* tmModes[] = { "ACES Film", /*"Gran Turismo 7"*/ };
			//int currentTM = (int)dbg.tonemapper;

			//if (ImGui::Combo("Mode", &currentTM, tmModes, IM_ARRAYSIZE(tmModes))) {
			//	dbg.tonemapper = (uint32_t)currentTM;
			//}

			auto& exposure = profiler.toneMappingSettings.cameraExposure;
			ImGui::SliderFloat("Exposure", &exposure, 0.01f, 0.3f);
		}


		ImGui::NewLine();
		UI::separatorText("Chromatic Aberration");

		{
			bool ca = dbg.enableChromaticAberration != 0u;
			if (ImGui::Checkbox("Enable Chromatic Aberration##post", &ca)) {
				dbg.enableChromaticAberration = ca ? 1u : 0u;
			}
		}

		ImGui::NewLine();
		UI::separatorText("Lens Flare");

		{
			auto& flareSettings = profiler.lensFlareSettings;
			bool flareOn = dbg.enableLensFlare != 0u;

			if (ImGui::Checkbox("Enable Lens Flare##post", &flareOn)) {
				dbg.enableLensFlare = flareOn ? 1u : 0u;
			}

			ImGui::Separator();
			ImGui::TextUnformatted("Ring");
			ImGui::Separator();

			ImGui::SliderFloat("Inner Radius##lf", &flareSettings.ringInnerRadius, 0.0001f, 0.3f, "%.5f");
			ImGui::SliderFloat("Outer Radius##lf", &flareSettings.ringOuterRadius, 0.001f, 0.3f, "%.5f");

			ImGui::Separator();
			ImGui::TextUnformatted("Streaks");
			ImGui::Separator();

			ImGui::SliderFloat("Streak Strength##lf", &flareSettings.streakStrength, 0.0f, 0.5f, "%.2f");
			ImGui::SliderFloat("Streak Width##lf", &flareSettings.streakWidth, 0.01f, 0.05f, "%.2f");
			ImGui::SliderFloat("Streak Length##lf", &flareSettings.streakLength, 0.0f, 0.5f, "%.2f");
		}
	}

	static void drawCategoryPipelines(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;
		//Profiler& profiler = *ui.profiler;

		UI::separatorText("Pipeline Views");

		{
			bool obb = dbg.enableOBBs != 0u;
			if (ImGui::Checkbox("Enable OBB##pipes", &obb)) {
				dbg.enableOBBs = obb ? 1u : 0u;
			}
		}

		// TODO: Add check box to turn on wireframe view, and setup shader
	}

	// For the profiler window
	static void drawCategoryProfiling(UIContext& ui)
	{
		//RD::RenderToggles& dbg = *ui.dbg;
		Profiler& profiler = *ui.profiler;
		FrameStats& stats = *ui.stats;

		UI::separatorText("Frame");

		//ImGui::Text("Delta Raw: %.3f ms", stats.deltaSecondsRaw * 1000.0f);

		ImGui::Text("Frame Time True: %.3f ms", stats.frameTimeRaw.Get());
		ImGui::Text("Frame Time Capped: %.3f ms", stats.frameTime.Get());
		ImGui::Text("FPS: %.1f", stats.fps.Get());

		{
			bool capEnabled = stats.capFramerate;
			if (ImGui::Checkbox("Cap Framerate", &capEnabled)) {
				stats.capFramerate = capEnabled;
			}
			{
				ImGui::BeginDisabled(!stats.capFramerate);

				const float buttonWidth = 60.0f;

				if (ImGui::Button("60", ImVec2(buttonWidth, 0))) {
					stats.targetFrameRate = RD::TARGET_FPS_60;
				}
				ImGui::SameLine();

				if (ImGui::Button("120", ImVec2(buttonWidth, 0))) {
					stats.targetFrameRate = RD::TARGET_FPS_120;
				}
				ImGui::SameLine();

				if (ImGui::Button("144", ImVec2(buttonWidth, 0))) {
					stats.targetFrameRate = RD::TARGET_FPS_144;
				}
				ImGui::SameLine();

				if (ImGui::Button("240", ImVec2(buttonWidth, 0))) {
					stats.targetFrameRate = RD::TARGET_FPS_240;
				}

				ImGui::EndDisabled();
			}
		}

		ImGui::Separator();
		ImGui::TextUnformatted("GPU update timings");
		ImGui::Text("GPU total:  %.3f ms", stats.gpuFrameTime.Get());

		ImGui::Separator();
		ImGui::TextUnformatted("CPU update timings");
		ImGui::Text("Draw:  %.3f ms", stats.drawTime.Get());
		ImGui::Text("Scene: %.3f ms", stats.sceneUpdateTime.Get());

		ImGui::Separator();
		ImGui::TextUnformatted("GPU Info");
		ImGui::Text(
			"VRAM: %llu / %llu MB",
			stats.vramStats.used / (1024ull * 1024ull),
			stats.vramStats.budget / (1024ull * 1024ull)
		);
		ImGui::Text("GPU: %s", stats.gpuName.c_str());

		// TODO: Figure out a different way to get present this data
		//ImGui::Separator();
		//ImGui::TextUnformatted("Model Data");
		//ImGui::Text("Meshes:     %u", dbg.meshCount);
		//ImGui::Text("Materials:  %u", dbg.materialCount);
		//ImGui::Text("Transforms: %u", dbg.transformCount);
		//ImGui::Text("Vertices:   %u", dbg.vertexCount);
		//ImGui::Text("Indices:    %u", dbg.indexCount);
		ImGui::Text("Triangles: %llu", (unsigned long long)stats.triangleCount);

		ImGui::Separator();
		ImGui::TextUnformatted("Draw Calls");

		const uint32_t indirectTotal =
			stats.opaqueIndirect.commands +
			stats.transparentIndirect.commands +
			stats.directionalCSMIndirect.commands +
			stats.flashlightShadowIndirect.commands;

		{
			UI::TableScope table(
				"DrawPathsTop",
				2,
				ImGuiTableFlags_SizingFixedFit |
				ImGuiTableFlags_BordersInnerV |
				ImGuiTableFlags_RowBg
			);

			if (table) {
				ImGui::TableSetupColumn("API");
				ImGui::TableSetupColumn("Commands");
				ImGui::TableHeadersRow();

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted("vkCmdDraw");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%u", stats.directDraws);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted("vkCmdDrawIndirect");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%u", indirectTotal);
			}
		}

		{
			UI::TableScope table(
				"IndirectBreakdown",
				3,
				ImGuiTableFlags_SizingFixedFit |
				ImGuiTableFlags_BordersInnerV |
				ImGuiTableFlags_RowBg
			);

			if (table) {
				ImGui::TableSetupColumn("Pass");
				ImGui::TableSetupColumn("Commands");
				ImGui::TableSetupColumn("Subdraws");
				ImGui::TableHeadersRow();

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted("Opaque");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%u", stats.opaqueIndirect.commands);
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%u", stats.opaqueIndirect.subdraws);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted("Transparent");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%u", stats.transparentIndirect.commands);
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%u", stats.transparentIndirect.subdraws);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted("Directional CSM");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%u", stats.directionalCSMIndirect.commands);
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%u", stats.directionalCSMIndirect.subdraws);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted("Flashlight Shadow");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%u", stats.flashlightShadowIndirect.commands);
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%u", stats.flashlightShadowIndirect.subdraws);
			}
		}

		if (profiler.IsTracyCompiledIn()) {
			ImGui::Text(
				"Render Pass Timings: %s",
				profiler.IsTracyGPUActive() ? "Active" : "Inactive"
			);
		}
		else {
			ImGui::TextUnformatted("Render Pass Timings: Disabled");
		}

		static bool showOnlyActivePasses = true;
		{
			UI::TableScope table(
				"PassTimings",
				3,
				ImGuiTableFlags_SizingStretchProp |
				ImGuiTableFlags_BordersInnerV |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_ScrollY
			);

			if (table) {
				ImGui::TableSetupColumn("Pass");
				ImGui::TableSetupColumn("CPU ms");
				ImGui::TableSetupColumn("GPU ms");
				ImGui::TableHeadersRow();

				const auto& allPassStats = profiler.GetAllPassStats();

				for (size_t passIndex = 1; passIndex < allPassStats.size(); ++passIndex) {
					const RD::Renderer_Pass passID = static_cast<RD::Renderer_Pass>(passIndex);
					const PassTimingStats& passStats = allPassStats[passIndex];

					if (showOnlyActivePasses && !passStats.activeLastFrame) continue;

					ImGui::TableNextRow();

					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(profiler.GetPassName(passID));

					ImGui::TableSetColumnIndex(1);
					if (passStats.cpuMsAverage.IsInitialized()) {
						ImGui::Text("%.3f", passStats.cpuMsAverage.Get());
					}
					else {
						ImGui::TextUnformatted("--");
					}

					ImGui::TableSetColumnIndex(2);
					if (passStats.gpuMsAverage.IsInitialized()) {
						ImGui::Text("%.3f", passStats.gpuMsAverage.Get());
					}
					else {
						ImGui::TextUnformatted("--");
					}
				}
			}
		}
	}

	static void panelSettingsWindow(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;

		if (!dbg.enableSettings) return;

		ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(Editor::SETTINGS_SIZE_X, Editor::SETTINGS_SIZE_Y), ImGuiCond_FirstUseEver);

		UI::WindowScope window(
			"Settings",
			nullptr,
			ImGuiWindowFlags_NoCollapse);

		if (!window) return;

		static Editor::SettingsCategory selectedCategory = Editor::SettingsCategory::Render;

		const float leftWidth = 130.0f;

		if (ImGui::BeginChild("SettingsLeft", ImVec2(leftWidth, 0.0f), true)) {
			DrawCategorySelector(selectedCategory);
		}
		ImGui::EndChild();

		ImGui::SameLine();

		if (ImGui::BeginChild("SettingsRight", ImVec2(0.0f, 0.0f), true)) {
			switch (selectedCategory)
			{
			case Editor::SettingsCategory::Render:
				DrawCategoryRender(ui);
				break;

			case Editor::SettingsCategory::Lighting:
				drawCategoryLighting(ui);
				break;

			case Editor::SettingsCategory::PostFX:
				drawCategoryPostFX(ui);
				break;

			case Editor::SettingsCategory::Pipelines:
				drawCategoryPipelines(ui);
				break;

			default:
				break;
			}
		}
		ImGui::EndChild();
	}

	static void panelProfilerWindow(UIContext& ui)
	{
		//Profiler& profiler = *ui.profiler;
		//FrameStats& stats = *ui.stats;
		RD::RenderToggles& dbg = *ui.dbg;

		if (!dbg.enableProfilerView) return;

		ImGuiIO& io = ImGui::GetIO();

		const float rightPadding = 10.0f;
		const float topPadding = 10.0f;

		ImVec2 profilerPos = ImVec2(
			io.DisplaySize.x - Editor::PROFILER_SIZE_X - rightPadding,
			topPadding
		);

		ImGui::SetNextWindowPos(profilerPos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(Editor::PROFILER_SIZE_X, Editor::PROFILER_SIZE_Y), ImGuiCond_Always);

		UI::WindowScope window(
			"Profiler",
			nullptr,
			ImGuiWindowFlags_NoCollapse);

		if (!window) return;

		drawCategoryProfiling(ui);
	}
}

void Editor::InitImgui(
	Renderer& renderer,
	GLFWwindow* window)
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
	pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
	pool_info.pPoolSizes = pool_sizes;


	const auto& deviceCtx = renderer.GetDevice().GetContext();

	const auto& graphicsQ = renderer.GetDevice().GetGraphicsQueue();

	VK_CHECK(vkCreateDescriptorPool(deviceCtx.device, &pool_info, nullptr, &m_imguiPool));

	// this initializes the core structures of imgui
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable keyboard controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Enable gamepad controls
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; // Prevent ImGui from overriding the cursor
	io.IniFilename = nullptr; // Won't create imgui file

	ImGui_ImplGlfw_InitForVulkan(window, true);

	// this initializes imgui for Vulkan
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = deviceCtx.instance;
	init_info.PhysicalDevice = deviceCtx.physicalDevice;
	init_info.Device = deviceCtx.device;
	init_info.Queue = graphicsQ.GetQueue();
	init_info.DescriptorPool = m_imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.UseDynamicRendering = true;

	//dynamic rendering parameters for imgui to use
	VkFormat swapchainFormat = renderer.GetSwapchain().GetFormat();
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchainFormat;

	init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info);

	glfwSetWindowFocusCallback(window, MyWindowFocusCallback);
}

void Editor::Shutdown(Renderer& renderer)
{
	ImGui_ImplVulkan_Shutdown();
	vkDestroyDescriptorPool(renderer.GetDevice().GetContext().device, m_imguiPool, nullptr);
}

void Editor::RenderImgui(Renderer& renderer)
{
	ImGui_ImplGlfw_NewFrame();
	ImGui_ImplVulkan_NewFrame();
	ImGui::NewFrame();

	UIContext ui;
	ui.profiler = &renderer.GetProfiler();
	ui.stats = &renderer.GetFrameStats();
	ui.dbg = &renderer.GetRenderToggles();

	static PanelRegistry registry;
	static bool didInit = false;

	if (!didInit)
	{
		didInit = true;

		registry.addPanel("SettingsWindow", &panelSettingsWindow);
		registry.addPanel("ProfilerWindow", [](UIContext& ui) {
			panelProfilerWindow(ui);
		});
	}

	registry.draw(ui);

	ImGui::Render();
}

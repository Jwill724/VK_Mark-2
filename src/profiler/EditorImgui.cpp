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

namespace RD = RendererDefinitions;

static void MyWindowFocusCallback(GLFWwindow* window, int focused)
{
	ImGui_ImplGlfw_WindowFocusCallback(window, focused);
}

namespace
{
	// =========================================================================
	// 1. Style
	// =========================================================================
	namespace Style
	{
		static const float CATEGORY_LIST_WIDTH = 130.0f;
		static const float METRIC_LABEL_WEIGHT = 0.58f;
		static const float WINDOW_PADDING      = 10.0f;

		static const ImVec4 ACCENT     = ImVec4(0.40f, 0.80f, 1.00f, 1.00f);
		static const ImVec4 GOOD       = ImVec4(0.45f, 0.85f, 0.45f, 1.00f);
		static const ImVec4 WARN       = ImVec4(1.00f, 0.75f, 0.30f, 1.00f);
		static const ImVec4 BAD        = ImVec4(1.00f, 0.40f, 0.40f, 1.00f);
		static const ImVec4 MUTED      = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);

		static const ImVec4 ROW_ASYNC  = ImVec4(0.10f, 0.24f, 0.34f, 0.55f);
		static const ImVec4 BAR_GFX    = ImVec4(0.30f, 0.48f, 0.70f, 0.90f);
		static const ImVec4 BAR_ASYNC  = ImVec4(0.20f, 0.60f, 0.80f, 0.90f);
	}

	// =========================================================================
	// 2. UI
	// =========================================================================
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

		struct ChildScope
		{
			bool isOpen = false;

			ChildScope(const char* id, const ImVec2& size, bool border)
			{
				isOpen = ImGui::BeginChild(id, size, border);
			}

			~ChildScope()
			{
				ImGui::EndChild();
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
				ImGuiTableFlags flags,
				const ImVec2& outerSize = ImVec2(0.0f, 0.0f))
			{
				isOpen = ImGui::BeginTable(id, columnCount, flags, outerSize);
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

		struct ColorScope
		{
			ColorScope(ImGuiCol index, const ImVec4& color)
			{
				ImGui::PushStyleColor(index, color);
			}

			~ColorScope()
			{
				ImGui::PopStyleColor();
			}
		};

		struct DisabledScope
		{
			DisabledScope(bool disabled)
			{
				ImGui::BeginDisabled(disabled);
			}

			~DisabledScope()
			{
				ImGui::EndDisabled();
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

		static void textRightAligned(const char* text, const ImVec4* color)
		{
			const float available = ImGui::GetContentRegionAvail().x;
			const float textWidth = ImGui::CalcTextSize(text).x;

			if (textWidth < available) {
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - textWidth));
			}

			if (color) {
				ImGui::TextColored(*color, "%s", text);
			}
			else {
				ImGui::TextUnformatted(text);
			}
		}

		// Label on the left, value right aligned. Every read-only number in the
		// editor goes through this so columns line up across sections.
		struct MetricTable
		{
			bool isOpen = false;

			MetricTable(const char* id, float labelWeight = Style::METRIC_LABEL_WEIGHT)
			{
				isOpen = ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp);

				if (isOpen) {
					ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, labelWeight);
					ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 1.0f - labelWeight);
				}
			}

			~MetricTable()
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

		static void metricImpl(const char* label, const std::string& value, const ImVec4* color)
		{
			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(label);

			ImGui::TableSetColumnIndex(1);
			textRightAligned(value.c_str(), color);
		}

		static void metric(const char* label, const std::string& value)
		{
			metricImpl(label, value, nullptr);
		}

		static void metric(const char* label, const std::string& value, const ImVec4& color)
		{
			metricImpl(label, value, &color);
		}

		static void bar(float fraction, const char* overlay, const ImVec4& color)
		{
			if (fraction < 0.0f) fraction = 0.0f;
			if (fraction > 1.0f) fraction = 1.0f;

			ColorScope barColor(ImGuiCol_PlotHistogram, color);
			ImGui::ProgressBar(fraction, ImVec2(-1.0f, ImGui::GetTextLineHeight()), overlay);
		}

		static std::string formatCount(uint64_t count)
		{
			if (count >= 1000000ull) {
				return fmt::format("{:.2f}M", double(count) / 1e6);
			}
			if (count >= 1000ull) {
				return fmt::format("{:.1f}K", double(count) / 1e3);
			}
			return fmt::format("{}", count);
		}
	}

	// =========================================================================
	// 3. UIWidgets
	// =========================================================================
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

		static bool comboU32(
			const char* label,
			uint32_t* valueU32,
			const char* const items[],
			int itemCount)
		{
			int current = static_cast<int>(*valueU32);

			if (!ImGui::Combo(label, &current, items, itemCount)) {
				return false;
			}

			*valueU32 = static_cast<uint32_t>(current);
			return true;
		}
	}

	// =========================================================================
	// 4. Context
	// =========================================================================
	struct UIContext
	{
		Profiler* profiler = nullptr;
		FrameStats* stats = nullptr;
		RD::RenderToggles* dbg = nullptr;
	};

	using DrawFn = void(*)(UIContext& ui);

	// One collapsible block of controls or readouts. Categories and the
	// profiler window are both just lists of these.
	struct ControlGroup
	{
		const char* label = "";
		DrawFn fn = nullptr;
		bool defaultOpen = true;
	};

	static void drawGroups(UIContext& ui, const ControlGroup* groups, int groupCount)
	{
		for (int i = 0; i < groupCount; ++i)
		{
			const ControlGroup& group = groups[i];
			if (!group.fn) continue;

			UI::IdScope id(group.label);

			const ImGuiTreeNodeFlags flags =
				group.defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0;

			if (ImGui::CollapsingHeader(group.label, flags)) {
				group.fn(ui);
				ImGui::Spacing();
			}
		}
	}

	using PanelVisibleFn = bool(*)(const UIContext& ui);

	struct Panel
	{
		const char* name = "";
		DrawFn fn = nullptr;
		PanelVisibleFn isVisible = nullptr;   // null = always drawn
	};

	struct PanelRegistry
	{
		std::vector<Panel> panels;

		void addPanel(const char* name, DrawFn fn, PanelVisibleFn isVisible = nullptr)
		{
			Panel panel;
			panel.name = name;
			panel.fn = fn;
			panel.isVisible = isVisible;
			panels.push_back(panel);
		}

		void draw(UIContext& ui) const
		{
			for (const Panel& panel : panels) {
				if (!panel.fn) continue;
				if (panel.isVisible && !panel.isVisible(ui)) continue;

				UI::IdScope id(panel.name);
				panel.fn(ui);
			}
		}
	};

	// -------------------------------------------------------------------------
	// Shared reads. Nothing here writes to the profiler.
	// -------------------------------------------------------------------------
	static float passGpuMs(const PassTimingStats& stats)
	{
		return stats.gpuMsAverage.IsInitialized() ? stats.gpuMsAverage.Get() : 0.0f;
	}

	static float passCpuMs(const PassTimingStats& stats)
	{
		return stats.cpuMsAverage.IsInitialized() ? stats.cpuMsAverage.Get() : 0.0f;
	}

	// =========================================================================
	// 5. Settings groups
	// =========================================================================
	static void groupCamera(UIContext& ui)
	{
		auto& camera = World::GetScene().GetCamera();

		const auto& pos = camera.GetPosition();
		const auto& camVelo = camera.GetVelocity();

		{
			UI::MetricTable table("CameraState");
			if (table) {
				UI::metric("World Position", fmt::format("{:.2f} {:.2f} {:.2f}", pos.x, pos.y, pos.z));
				UI::metric("Velocity", fmt::format("{:.2f} {:.2f} {:.2f}", camVelo.x, camVelo.y, camVelo.z));
			}
		}

		ImGui::Spacing();

		float camSens = camera.GetSensitivity();
		if (ImGui::SliderFloat("Sensitivity##cam", &camSens, 1.0f, 100.0f, "%.0f")) {
			camera.SetSensitivity(camSens);
		}

		float camFOV = camera.GetFovY();
		if (ImGui::SliderFloat("FOV##cam", &camFOV, Camera::CAMERA_MIN_FOV, Camera::CAMERA_MAX_FOV, "%.0f")) {
			camera.SetFovY(camFOV);
		}

		float maxSpeed = camera.GetMaxSpeed();
		if (ImGui::SliderFloat("Max Speed##cam", &maxSpeed, 1.0f, 100.0f, "%.0f")) {
			camera.SetMaxSpeed(maxSpeed);
		}

		float minSpeed = camera.GetMinSpeed();
		if (ImGui::SliderFloat("Min Speed##cam", &minSpeed, 1.0f, 100.0f, "%.0f")) {
			camera.SetMinSpeed(minSpeed);
		}

		float accel = camera.GetAcceleration();
		if (ImGui::SliderFloat("Acceleration##cam", &accel, 1.0f, 100.0f, "%.0f")) {
			camera.SetAcceleration(accel);
		}

		float damping = camera.GetDamping();
		if (ImGui::SliderFloat("Damping##cam", &damping, 1.0f, 100.0f, "%.0f")) {
			camera.SetDamping(damping);
		}
	}

	static void groupAsyncCompute(UIContext& ui)
	{
		Profiler& profiler = *ui.profiler;
		const auto& async = profiler.asyncStats;

		if (!async.bDedicatedQueue)
		{
			ImGui::TextDisabled("Unavailable");
			ImGui::TextWrapped(
				"This device exposes no compute queue family distinct from "
				"graphics. The graph always uses the single-submit path.");
			return;
		}

		ImGui::Checkbox("Enable Async Compute##async", &profiler.enableAsyncCompute);

		//ImGui::Spacing();

		//UI::MetricTable table("AsyncState");
		//if (!table) return;

		//UI::metric("Graphics batches", fmt::format("{}", async.graphicsBatchCount));
		//UI::metric("Async passes", fmt::format("{}", async.asyncPassCount));
		//UI::metric("Overlapped passes", fmt::format("{}", async.overlapPassCount));
		//UI::metric(
		//	"Active this frame",
		//	async.bActiveThisFrame ? "yes" : "no",
		//	async.bActiveThisFrame ? Style::ACCENT : Style::MUTED);
	}

	static void groupAntiAliasing(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;
		Profiler& profiler = *ui.profiler;

		auto& taaSettings = profiler.taaSettings;

		ImGui::SliderFloat("Min Blend", &taaSettings.minBlend, 0.01f, 1.0f, "%.2f");
		ImGui::SliderFloat("Max Blend", &taaSettings.maxBlend, 0.85f, 1.0f, "%.2f");
		ImGui::SliderFloat("Depth Disocclusion Scale", &taaSettings.depthDisocclusionScale, 1.0f, 30.0f);
		ImGui::SliderFloat("Normal Disocclusion Scale", &taaSettings.normalDisocclusionScale, 5.0f, 25.0f);
		ImGui::SliderFloat("Dark Clamp Boost", &taaSettings.darkClampBoost, 1.25f, 2.5f, "%.2f");
		ImGui::SliderFloat("Sigma Floor Scale", &taaSettings.sigmaFloorScale, 0.01f, 0.05f, "%.2f");
		ImGui::SliderFloat("Shading Change Scale", &taaSettings.shadingChangeScale, 0.1f, 5.0f);
		ImGui::SliderFloat("Sharpen", &taaSettings.sharpen, 0.0f, 1.0f);
	}

	static void groupTransparency(UIContext& ui)
	{
		Profiler& profiler = *ui.profiler;
		ImGui::SliderFloat("OIT Z Scale", &profiler.forwardPush.oitDepthScale, 50.0f, 2000.0f, "%.0f");
	}

	static void groupSun(UIContext& ui)
	{
		(void)ui;

		auto& scene = World::GetScene().GetSceneData();

		static glm::vec3 sunCol = glm::vec3(scene.sunlightColor);
		static float sunI = scene.sunlightColor.w;
		static glm::vec3 sunDir = glm::vec3(scene.sunlightDirection);

		ImGui::SliderFloat3("Sun Dir##light", glm::value_ptr(sunDir), -0.5f, 0.5f);
		ImGui::SliderFloat3("Sun Color##light", glm::value_ptr(sunCol), 0.0f, 1.0f);

		int sunInt = static_cast<float>(sunI);
		ImGui::SliderInt("Sun Intensity##light", &sunInt, 0, 100);
		sunI = static_cast<float>(sunInt);

		scene.sunlightColor = glm::vec4(sunCol, sunI);
		scene.sunlightDirection = glm::vec4(sunDir, 0.0f);
	}

	static void groupEnvironment(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;

		static int selectedEnv = 0;

		if (ImGui::BeginCombo("Active##env", fmt::format("Image {}", selectedEnv + 1).c_str())) {
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
	}

	static void groupLocalLights(UIContext& ui)
	{
		(void)ui;

		bool dynamicLights = LightingSystem::_dynamicLightsEnabled;
		if (ImGui::Checkbox("Dynamic Lights##light", &dynamicLights)) {
			LightingSystem::_dynamicLightsEnabled = dynamicLights ? 1u : 0u;
		}

		static uint32_t targetLightCount = 0u;
		if (UIWidgets::sliderU32("Light Count##light", &targetLightCount, 0u, static_cast<uint32_t>(RD::MAX_VISIBLE_LIGHTS))) {
			LightingSystem::SetTargetActiveLightCount(targetLightCount);
		}

		const uint32_t activeCount = LightingSystem::GetActiveLightCount();

		UI::MetricTable table("LightCounts");
		if (table) {
			UI::metric("Active", fmt::format("{} / {}", activeCount, static_cast<uint32_t>(RD::MAX_VISIBLE_LIGHTS)));
		}
	}

	static void groupFlashlight(UIContext& ui)
	{
		(void)ui;

		auto& flashlight = LightingSystem::_flashlightSettings;
		auto& flashlightReal = LightingSystem::_mainFlashLight;

		ImGui::SliderFloat("Lag Strength", &flashlightReal.m_lagStrength, 10.0, 100.0f);
		ImGui::SliderFloat("Sway Strength", &flashlightReal.m_swayStrength, 0.001f, 0.1f, "%.3f");
		ImGui::SliderFloat("Source Radius##light", &flashlight.sourceRadius, 0.02, 0.3f);
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
	}


	static void rtShadowParamsUI(RTShadowParams& p, const char* id, float tMaxMax)
	{
		auto label = [id](const char* name) {
			static char buf[96];
			snprintf(buf, sizeof(buf), "%s##%s", name, id);
			return buf;
			};

		ImGui::SliderFloat(label("Ray TMin"), &p.rayTMin, 0.0001f, 0.1f, "%.4f",
			ImGuiSliderFlags_Logarithmic);
		ImGui::SliderFloat(label("Ray TMax"), &p.rayTMax, 5.0f, tMaxMax);

		ImGui::SliderFloat(label("Normal Bias"), &p.normalBias, 0.0f, 0.25f, "%.3f");
		ImGui::SetItemTooltip("Flat world-space offset along the surface normal.");

		ImGui::SliderFloat(label("Slope Bias"), &p.rayBias, 0.0f, 1e-3f, "%.5f",
			ImGuiSliderFlags_Logarithmic);
		ImGui::SetItemTooltip("Scaled by distance from camera. Total = normal + slope * dist.");

		//ImGui::SliderFloat(label("Sun Softness"), &p.sunSoftness, 0.0f, 2.0f);
		//ImGui::SliderFloat(label("Shadow Mip Bias"), &p.mipBias, 0.0f, 6.0f);

		//int taps = static_cast<int>(p.taps);
		//if (ImGui::SliderInt(label("Shadow Taps"), &taps, 1, 4)) {
		//	p.taps = static_cast<uint32_t>(taps);
		//}

		//UIWidgets::toggleU32(label("Alpha Tested Casters"), &p.alphaTested);
	}

	static void groupShadows(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;
		Profiler& profiler = *ui.profiler;

		bool shadows = dbg.enableShadows != 0u;
		bool contact = dbg.enableSSS != 0u;

		if (ImGui::Checkbox("Enable Shadows##rt", &shadows)) {
			dbg.enableShadows = shadows ? 1u : 0u;
		}

		if (!dbg.enableShadows) return;

		auto& shadowControl = World::GetScene().GetShadowControls();

		ImGui::Spacing();
		UI::separatorText("Sun Shadow Filter");

		const char* sunFilterModes[] = { "PCF", "PCSS", "RT_Soft" };
		int sunFilter = static_cast<int>(dbg.sunShadowFilter);

		if (ImGui::Combo("Filter Mode##rt", &sunFilter, sunFilterModes, IM_ARRAYSIZE(sunFilterModes))) {
			dbg.sunShadowFilter = static_cast<uint32_t>(sunFilter);
		}

		const bool bRTSoft =
			dbg.sunShadowFilter == static_cast<uint32_t>(RD::SunShadowFilter::RT_SOFT);

		if (!bRTSoft)
		{
			const char* qualityModes[] = { "Low", "Medium", "High" };
			int currentQuality = static_cast<int>(profiler.shadowQuality);

			if (ImGui::Combo(
				"Shadow Quality",
				&currentQuality,
				qualityModes,
				IM_ARRAYSIZE(qualityModes)))
			{
				profiler.shadowQuality =
					static_cast<RD::ShadowQuality>(currentQuality);
			}

			int shadowFar = static_cast<int>(shadowControl.shadowFar);

			ImGui::SliderInt(
				"Shadow Far##rt",
				&shadowFar,
				500,
				1500);

			if (shadowFar != static_cast<int>(shadowControl.shadowFar))
			{
				shadowControl.shadowFar = static_cast<float>(shadowFar);
				World::GetScene().ShouldUpdateCascadeSplits();
			}

			bool depthHack = shadowControl.enableShadowDepthExtendHack;

			if (ImGui::Checkbox("Enable Depth Hack##rt", &depthHack))
			{
				shadowControl.enableShadowDepthExtendHack = depthHack;
				World::GetScene().ShouldUpdateCascadeSplits();
			}

			if (dbg.sunShadowFilter == static_cast<uint32_t>(RD::SunShadowFilter::PCSS))
			{
				ImGui::SliderFloat(
					"Sun Angular Radius##rt",
					&shadowControl.sunAngularRadiusDeg,
					0.0f,
					6.0f,
					"%.2f deg");

				ImGui::SliderFloat(
					"Min Radius (texels)##rt",
					&shadowControl.minFilterRadiusTexels,
					0.25f,
					4.0f,
					"%.2f");

				ImGui::SliderFloat(
					"Search Scale##rt",
					&shadowControl.searchRadiusScale,
					0.5f,
					2.0f,
					"%.2f");

				ImGui::SliderFloat(
					"Max Normal Offset##rt",
					&shadowControl.maxNormalOffsetTexels,
					1.0f,
					12.0f,
					"%.1f");

				ImGui::DragFloat4(
					"Max Radius (texels)##rt",
					&shadowControl.pcssMaxRadiusTexels.x,
					0.1f,
					1.0f,
					48.0f,
					"%.1f");
			}
		}
		else
		{
			rtShadowParamsUI(
				profiler.rtShadowPush.shadow,
				"rtsh",
				1500.0f);
		}

		ImGui::Spacing();
		UI::separatorText("Contact Shadows");

		if (ImGui::Checkbox("Enable Screen Space Contact Shadows##rt", &contact)) {
			dbg.enableSSS = contact ? 1u : 0u;
		}

		if (dbg.enableSSS)
		{
			auto& contactShadowSettings = profiler.contactShadowsSettings;

			ImGui::SliderFloat(
				"Surface Thickness##rt",
				&contactShadowSettings.surfaceThickness,
				0.001f,
				0.02f,
				"%.3f");

			ImGui::SliderFloat(
				"Bilinear Threshold##rt",
				&contactShadowSettings.bilinearThreshold,
				0.001f,
				0.5f,
				"%.3f");

			int shadowContrastInt =
				static_cast<int>(contactShadowSettings.shadowContrast);

			ImGui::SliderInt(
				"Shadow Contrast##rt",
				&shadowContrastInt,
				1,
				8);

			contactShadowSettings.shadowContrast =
				static_cast<float>(shadowContrastInt);
		}
	}

	static void groupSSGI(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;
		Profiler& profiler = *ui.profiler;

		const char* giModes[] = { "Off", "VBAO", "VBGI" };
		UIWidgets::comboU32("GI Method", &dbg.giMode, giModes, IM_ARRAYSIZE(giModes));

		if (dbg.giMode == static_cast<uint32_t>(RD::GIMethod::OFF)) return;

		auto& s = profiler.ssgiSettings;

		ImGui::SeparatorText("Trace");
		ImGui::SliderFloat("Effect Radius##ssgi", &s.effectRadius, 0.5f, 25.0f);
		ImGui::SetItemTooltip("Occlusion radius, and the GI gather radius. Wider costs dependent fetches.");
		ImGui::SliderFloat("Falloff Range##ssgi", &s.effectFalloffRange, 0.0f, 1.0f);

		ImGui::SliderFloat("Sample Distribution Power##ssgi", &s.sampleDistributionPower, 1.0, 3.0);

		ImGui::SeparatorText("Denoise");
		ImGui::SliderFloat("Blur Beta##ssgi", &s.denoiseBlurBeta, 1.0f, 2.0f);
		ImGui::SliderFloat("Upsample Depth Sigma##ssgi", &s.upsampleDepthSigma, 32.0f, 1024.0f);

		if (dbg.giMode != static_cast<uint32_t>(RD::GIMethod::VBGI)) return;

		auto& t = profiler.forwardPush;

		ImGui::SeparatorText("Indirect");
		ImGui::SliderFloat("GI Intensity##ssgi", &t.giIntensity, 0.0f, 25.0f);
		ImGui::SliderFloat("Bounce Feedback##ssgi", &t.bounceFeedback, 0.0f, 1.0f);
		ImGui::SetItemTooltip("Gain on the multi-bounce loop. Above ~0.85 bright rooms keep brightening.");
		ImGui::SliderFloat("SH Fallback##ssgi", &s.giFallbackStrength, 0.0f, 1.0f);
		ImGui::SetItemTooltip("Sky fill for sectors the trace closed but could not gather.");

		ImGui::SeparatorText("Temporal");
		ImGui::SliderFloat("Temporal Alpha##ssgi", &s.giTemporalAlpha, 0.02f, 0.5f, "%.3f");
		ImGui::SetItemTooltip("Steady-state EMA rate. 0.08 converges over ~12 frames.");
		ImGui::SliderFloat("Reproject Tolerance##ssgi", &s.giReprojTolerance, 0.02f, 0.3f, "%.3f");
		ImGui::SliderFloat("Firefly Clamp##ssgi", &s.giClampMax, 0.5f, 32.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
	}

	static void groupReflections(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;
		Profiler& profiler = *ui.profiler;

		auto& rs = profiler.reflectPush;

		UIWidgets::toggleU32("Enable RT Reflections##rtr", &dbg.enableRTReflections);

		ImGui::SeparatorText("Ray");

		ImGui::SliderFloat("Reflect Roughness Cutoff##rtr", &rs.reflectRoughnessCutoff, 0.05f, 1.0f);

		int maxBouncesInt = static_cast<int>(rs.maxBounces);
		if (ImGui::SliderInt("Max Bounces##rtr", &maxBouncesInt, 2, 3)) {
			rs.maxBounces = static_cast<uint32_t>(maxBouncesInt);
		}

		int maxReflectLightsInt = static_cast<int>(rs.maxReflectLights);
		if (ImGui::SliderInt("Max Reflected Lights##rtr", &maxReflectLightsInt, 50, 300)) {

			rs.maxReflectLights = static_cast<uint32_t>(maxReflectLightsInt);
		}

		//ImGui::SeparatorText("Shadow Rays");
		//rtShadowParamsUI(rs.shadow, "rtr", 500.0f);

		ImGui::SeparatorText("Resolve");
		ImGui::SliderFloat("Roughness Fade##rtr", &rs.roughnessFadeStart, 0.0f, 1.0f);
		ImGui::SliderFloat("Ambient Scale##rtr", &rs.ambientScale, 0.0f, 4.0f);
	}

	static void groupVolumetrics(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;
		Profiler& profiler = *ui.profiler;

		auto& volSettings = profiler.volLightSettings;

		UIWidgets::toggleU32("Enable Volumetrics##vol", &dbg.enableVolumetrics);

		UI::separatorText("Fog");

		ImGui::SliderFloat("Density##vol", &volSettings.density, 0.0f, 0.03f, "%.3f");
		ImGui::SliderFloat("Scattering##vol", &volSettings.scatteringStrength, 0.0f, 10.0f);
		ImGui::SliderFloat("Extinction##vol", &volSettings.extinction, 0.0f, 1.0f);
		ImGui::SliderFloat("Asymmetry Factor##vol", &volSettings.asymmetryFactor, 0.0f, 0.95f, "%.2f");
		ImGui::SliderFloat("Height Falloff##vol", &volSettings.heightFalloff, 0.0f, 0.1f, "%.2f");

		UI::separatorText("Ray March");

		ImGui::SliderFloat("Max Distance##vol", &volSettings.maxDistance, 10.0f, 300.0f);
		ImGui::SliderFloat("Jitter Strength##vol", &volSettings.jitterStrength, 0.0f, 1.0f);

		UI::separatorText("Temporal");
		ImGui::SliderFloat("History Weight##vol", &volSettings.historyWeight, 0.85f, 0.95f, "%.2f");
		ImGui::SliderFloat("Clip Gamma##vol", &volSettings.clipGamma, 1.0f, 2.0f, "%.2f");

		UI::separatorText("Blur");

		ImGui::SliderFloat("Blur Radius##vol", &volSettings.blurRadius, 1.0f, 4.0f, "%.0f");
		ImGui::SliderFloat("Blur Depth Sigma##vol", &volSettings.blurDepthSigma, 0.1f, 2.0f, "%.2f");
		ImGui::SliderFloat("Blur Weight Sigma##vol", &volSettings.blurWeightSigma, 0.5f, 2.0f, "%.2f");
	}

	static void groupToneMapping(UIContext& ui)
	{
		Profiler& profiler = *ui.profiler;

		//const char* tmModes[] = { "ACES Film", /*"Gran Turismo 7"*/ };
		//int currentTM = (int)dbg.tonemapper;

		//if (ImGui::Combo("Mode", &currentTM, tmModes, IM_ARRAYSIZE(tmModes))) {
		//	dbg.tonemapper = (uint32_t)currentTM;
		//}

		auto& exposure = profiler.toneMappingSettings.cameraExposure;
		ImGui::SliderFloat("Exposure", &exposure, 0.01f, 0.3f);
	}

	static void groupBloom(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;
		Profiler& profiler = *ui.profiler;

		UIWidgets::toggleU32("Enable Bloom##post", &dbg.enableBloom);

		if (!dbg.enableBloom) return;

		auto& bloom = profiler.bloomPush;
		ImGui::SliderFloat("Bloom Intensity", &profiler.debugToggles.bloomIntensity, 0.03, 0.2f, "%.2f");
		ImGui::SliderFloat("Bloom Threshold", &bloom.bloomThreshold, 0.01f, 3.0f, "%.2f");
		ImGui::SliderFloat("Bloom Knee", &bloom.bloomKnee, 0.01f, 2.0f, "%.2f");
		ImGui::SliderFloat("Emissive Boost", &bloom.emissiveBoost, 0.0, 10.0, "%.2f");
	}

	static void groupChromaticAberration(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;

		UIWidgets::toggleU32("Enable Chromatic Aberration##post", &dbg.enableChromaticAberration);
	}

	static void groupLensFlare(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;
		Profiler& profiler = *ui.profiler;

		auto& flareSettings = profiler.lensFlareSettings;

		UIWidgets::toggleU32("Enable Lens Flare##post", &dbg.enableLensFlare);

		UI::separatorText("Ring");

		ImGui::SliderFloat("Inner Radius##lf", &flareSettings.ringInnerRadius, 0.0001f, 0.3f, "%.5f");
		ImGui::SliderFloat("Outer Radius##lf", &flareSettings.ringOuterRadius, 0.001f, 0.3f, "%.5f");

		UI::separatorText("Streaks");

		ImGui::SliderFloat("Streak Strength##lf", &flareSettings.streakStrength, 0.0f, 0.5f, "%.2f");
		ImGui::SliderFloat("Streak Width##lf", &flareSettings.streakWidth, 0.01f, 0.05f, "%.2f");
		ImGui::SliderFloat("Streak Length##lf", &flareSettings.streakLength, 0.0f, 0.5f, "%.2f");
	}

	static void groupCullingDebug(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;

		UIWidgets::toggleU32("Disable Occlusion Culling##rt", &dbg.disableOcclusionCull);
	}

	static void groupDebugDraw(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;
		Profiler& profiler = *ui.profiler;

		UIWidgets::toggleU32("Show Opaque OBB##pipes", &dbg.showOpaqueOBBs);
		UIWidgets::toggleU32("Show Transparent OBB##pipes", &dbg.showTransparentOBBs);

		ImGui::Checkbox("Show Wireframe", &profiler.enableWireframeView);
	}

	static void groupDebugViews(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;

		int current = static_cast<int>(dbg.debugView);

		constexpr int columns = 3;
		int column = 0;

		for (const RD::DebugViewEntry& entry : RD::DEBUG_VIEW_TABLE)
		{
			const uint32_t viewIndex = static_cast<uint32_t>(entry.view);

			// Cascade visualization only applies to raster sun shadows.
			if (entry.view == RD::DebugView::Cascades &&
				(!dbg.enableShadows ||
					dbg.sunShadowFilter == static_cast<uint32_t>(RD::SunShadowFilter::RT_SOFT)))
			{
				continue;
			}

			if (column != 0) {
				ImGui::SameLine();
			}

			const std::string label = fmt::format("{}##dbgview", entry.label);

			if (ImGui::RadioButton(label.c_str(), &current, static_cast<int>(viewIndex))) {
				dbg.debugView = static_cast<uint32_t>(current);
			}

			column = (column + 1) % columns;
			if (column == 0) {
				ImGui::NewLine();
			}
		}

		if (column != 0) {
			ImGui::NewLine();
		}
	}

	// -------------------------------------------------------------------------
	// Category tables. Adding a control block is one row here; moving one
	// between categories is a cut and paste of that row.
	// -------------------------------------------------------------------------
	static const ControlGroup RENDER_GROUPS[] = {
		{ "Camera",           &groupCamera,         false },
		{ "Async Compute",    &groupAsyncCompute,   false },
		{ "Anti-Aliasing",    &groupAntiAliasing,   true  },
		{ "Transparency",     &groupTransparency,   false },
	};

	static const ControlGroup LIGHTING_GROUPS[] = {
		{ "Sun",                 &groupSun,              true  },
		{ "Shadows",             &groupShadows,          false },
		{ "Reflections",         &groupReflections,      false },
		{ "Global Illumination", &groupSSGI,             false },
		{ "Local Lights",        &groupLocalLights,      false },
		{ "Flash Light",         &groupFlashlight,       false },
		{ "Volumetrics",         &groupVolumetrics,      false },
		{ "Environment",         &groupEnvironment,      false },
	};

	static const ControlGroup POSTFX_GROUPS[] = {
		{ "Tone Mapping",         &groupToneMapping,         true  },
		{ "Bloom",                &groupBloom,               true  },
		{ "Chromatic Aberration", &groupChromaticAberration, false },
		{ "Lens Flare",           &groupLensFlare,           false },
	};

	static const ControlGroup DEBUG_GROUPS[] = {
		{ "Culling",         &groupCullingDebug, true },
		{ "Debug Draw",      &groupDebugDraw,    true },
		{ "Shading Overlay", &groupDebugViews,   true },
	};

	struct SettingsCategoryEntry
	{
		const char* label = "";
		const ControlGroup* groups = nullptr;
		int groupCount = 0;
	};

	static const SettingsCategoryEntry SETTINGS_CATEGORIES[] = {
		{ "Lighting", LIGHTING_GROUPS, IM_ARRAYSIZE(LIGHTING_GROUPS) },
		{ "Render",   RENDER_GROUPS,   IM_ARRAYSIZE(RENDER_GROUPS)   },
		{ "Post FX",  POSTFX_GROUPS,   IM_ARRAYSIZE(POSTFX_GROUPS)   },
		{ "Debug",    DEBUG_GROUPS,    IM_ARRAYSIZE(DEBUG_GROUPS)    },
	};

	static_assert(
		IM_ARRAYSIZE(SETTINGS_CATEGORIES) == static_cast<int>(Editor::SettingsCategory::Count),
		"Category table count mismatch.");

	static void drawCategorySelector(Editor::SettingsCategory& selectedCategory)
	{
		UI::separatorText("Categories");

		for (int categoryIndex = 0; categoryIndex < IM_ARRAYSIZE(SETTINGS_CATEGORIES); ++categoryIndex) {
			const bool isSelected = (static_cast<int>(selectedCategory) == categoryIndex);

			if (ImGui::Selectable(SETTINGS_CATEGORIES[categoryIndex].label, isSelected)) {
				selectedCategory = (Editor::SettingsCategory)categoryIndex;
			}
		}
	}

	// =========================================================================
	// 6. Profiler sections — read-only views over Profiler / FrameStats
	// =========================================================================
	static void sectionFrame(UIContext& ui)
	{
		FrameStats& stats = *ui.stats;

		const float fps = stats.fps.Get();
		const ImVec4& fpsColor =
			(fps >= 100.0f) ? Style::GOOD :
			(fps >=  45.0f) ? Style::WARN : Style::BAD;

		{
			UI::MetricTable table("FrameTimings");
			if (table) {
				UI::metric("FPS", fmt::format("{:.1f}", fps), fpsColor);
				UI::metric("Frame (capped)", fmt::format("{:.3f} ms", stats.frameTime.Get()));
				UI::metric("Frame (true)", fmt::format("{:.3f} ms", stats.frameTimeRaw.Get()));
				UI::metric("CPU draw", fmt::format("{:.3f} ms", stats.drawTime.Get()));
				UI::metric("CPU scene", fmt::format("{:.3f} ms", stats.sceneUpdateTime.Get()));
			}
		}

		ImGui::Spacing();

		ImGui::Checkbox("Cap Framerate", &stats.capFramerate);

		{
			UI::DisabledScope disabled(!stats.capFramerate);

			struct FpsPreset { const char* label; float value; };

			static const FpsPreset presets[] = {
				{ "60",  RD::TARGET_FPS_60  },
				{ "120", RD::TARGET_FPS_120 },
				{ "144", RD::TARGET_FPS_144 },
				{ "240", RD::TARGET_FPS_240 },
			};

			const float buttonWidth = 60.0f;

			for (int i = 0; i < IM_ARRAYSIZE(presets); ++i)
			{
				if (i != 0) ImGui::SameLine();

				const bool isActive = (stats.targetFrameRate == presets[i].value);

				UI::ColorScope highlight(
					ImGuiCol_Button,
					isActive ? Style::BAR_ASYNC : ImGui::GetStyle().Colors[ImGuiCol_Button]);

				if (ImGui::Button(presets[i].label, ImVec2(buttonWidth, 0))) {
					stats.targetFrameRate = presets[i].value;
				}
			}
		}
	}

	static void sectionGpu(UIContext& ui)
	{
		Profiler& profiler = *ui.profiler;
		FrameStats& stats = *ui.stats;

		float    graphicsGpuMs = 0.0f;
		float    asyncGpuMs    = 0.0f;
		//float    graphicsCpuMs = 0.0f;
		//float    asyncCpuMs    = 0.0f;
		//uint32_t graphicsCount = 0u;
		uint32_t asyncCount    = 0u;

		const auto& allPassStats = profiler.GetAllPassStats();
		for (const PassTimingStats& s : allPassStats)
		{
			if (!s.activeLastFrame) continue;

			const float gpu = passGpuMs(s);
			//const float cpu = s.cpuMsAverage.IsInitialized() ? s.cpuMsAverage.Get() : 0.0f;

			if (s.asyncQueueLastFrame)
			{
				asyncGpuMs += gpu;
				//asyncCpuMs += cpu;
				++asyncCount;
			}
			else
			{
				graphicsGpuMs += gpu;
				//graphicsCpuMs += cpu;
				//++graphicsCount;
			}
		}

		{
			UI::MetricTable table("GpuTimings");
			if (table) {
				UI::metric("Total", fmt::format("{:.3f} ms", stats.gpuFrameTime.Get()));
				UI::metric("Graphics queue", fmt::format("{:.3f} ms", graphicsGpuMs));

				if (asyncCount > 0u) {
					UI::metric("Async queue", fmt::format("{:.3f} ms", asyncGpuMs), Style::ACCENT);
				}
			}
		}

		// Queue split. Sums of per-pass averages, so it tracks the shape of the
		// frame rather than the wall clock total above.
		const float queueTotal = graphicsGpuMs + asyncGpuMs;
		if (queueTotal > 0.0f && asyncCount > 0u)
		{
			UI::bar(
				graphicsGpuMs / queueTotal,
				fmt::format("gfx {:.0f}%", 100.0f * graphicsGpuMs / queueTotal).c_str(),
				Style::BAR_GFX);
		}

		ImGui::Spacing();
		UI::separatorText("Device");

		const double usedMb = double(stats.vramStats.used) / (1024.0 * 1024.0);

		const double budgetMb = double(stats.vramStats.budget) / (1024.0 * 1024.0);

		const float vramFrac = (budgetMb > 0.0)
			? float(usedMb / budgetMb)
			: 0.0f;

		UI::bar(
			vramFrac,
			fmt::format(
				"{:.0f} / {:.0f} MB",
				usedMb,
				budgetMb).c_str(),
			(vramFrac > 0.9f)
			? Style::BAD
			: (vramFrac > 0.75f)
			? Style::WARN
			: Style::BAR_GFX);

		ImGui::TextWrapped("%s", stats.gpuName.c_str());
	}

	static void sectionAssets(UIContext& ui)
	{
		Profiler& profiler = *ui.profiler;

		UI::MetricTable table("AssetCounts");
		if (!table) return;

		UI::metric("Meshes", UI::formatCount(profiler.assetCounts.totalMeshCount));
		UI::metric("Materials", UI::formatCount(profiler.assetCounts.totalMaterialCount));
		UI::metric("Vertices", UI::formatCount(profiler.assetCounts.totalVertexCount));
		UI::metric("Indices", UI::formatCount(profiler.assetCounts.totalIndexCount));
	}

	static void sectionVisibility(UIContext& ui)
	{
		Profiler& profiler = *ui.profiler;

		// GPU driven rendering statistics
		const GPUStats& gpu = profiler.gpuStats;

		const uint32_t totalVisible = gpu.visibleOpaque + gpu.visibleTransparent;
		const uint32_t totalInstances = static_cast<uint32_t>(World::GetInstanceState().gpuInputs.size());

		{
			UI::MetricTable table("VisibilityCounts");
			if (table) {
				UI::metric("Visible Opaque", UI::formatCount(gpu.visibleOpaque));
				UI::metric("Visible Transparent", UI::formatCount(gpu.visibleTransparent));
				UI::metric("Shadow Casters", UI::formatCount(gpu.visibleShadowCasters));
				UI::metric("Triangles", UI::formatCount(gpu.triangleCount));
			}
		}

		if (totalInstances > 0)
		{
			const uint32_t culled = (totalInstances >= totalVisible)
				? totalInstances - totalVisible : 0u;
			const float cullRatio = float(culled) / float(totalInstances);

			ImGui::Spacing();
			UI::bar(
				cullRatio,
				fmt::format("culled {:.1f}%  ({} / {})", 100.0f * cullRatio, culled, totalInstances).c_str(),
				Style::BAR_GFX);
		}

		//ImGui::Spacing();
		//UI::separatorText("Draw Commands");

		//UI::MetricTable table("DrawCounts");
		//if (!table) return;

		//UI::metric("Opaque", UI::formatCount(gpu.opaqueDrawCount));
		//UI::metric("Transparent", UI::formatCount(gpu.transparentDrawCount));
		//UI::metric("Shadow", UI::formatCount(gpu.shadowDrawCount));
		////UI::metric("Total", UI::formatCount(
		////	gpu.opaqueDrawCount + gpu.transparentDrawCount + gpu.shadowDrawCount));
	}

	static void sectionMeshlets(UIContext& ui)
	{
		Profiler& profiler = *ui.profiler;

		const GPUStats& gpu = profiler.gpuStats;
		const uint32_t mlDrawn = gpu.meshletsDrawnEarly + gpu.meshletsDrawnLate;

		//if (gpu.meshletsSubmitted == 0u)
		//{
		//	ImGui::TextDisabled("No meshlet dispatches this frame.");
		//	return;
		//}

		//const bool bOvercount = (mlDrawn > gpu.meshletsSubmitted);

		//static uint32_t peakOvercount   = 0u;
		//static uint32_t overcountFrames = 0u;
		//static uint32_t quietFrames     = 0u;

		//constexpr uint32_t OVERCOUNT_DECAY_FRAMES = 60u;

		//if (bOvercount)
		//{
		//	peakOvercount = std::max(peakOvercount, mlDrawn - gpu.meshletsSubmitted);
		//	++overcountFrames;
		//	quietFrames = 0u;
		//}
		//else if (peakOvercount > 0u)
		//{
		//	if (++quietFrames >= OVERCOUNT_DECAY_FRAMES)
		//	{
		//		peakOvercount   = 0u;
		//		overcountFrames = 0u;
		//		quietFrames     = 0u;
		//	}
		//}

		//{
		//	UI::MetricTable table("MeshletCounts");
		//	if (table) {
		//		UI::metric("Submitted", UI::formatCount(gpu.meshletsSubmitted));

		//		if (bOvercount) {
		//			UI::metric("Drawn", UI::formatCount(mlDrawn), Style::BAD);
		//		}
		//		else {
		//			UI::metric("Drawn", UI::formatCount(mlDrawn));
		//		}

		//		UI::metric("Phase 1", UI::formatCount(gpu.meshletsDrawnEarly));
		//		UI::metric("Phase 2", UI::formatCount(gpu.meshletsDrawnLate));
		//	}
		//}
		//ImGui::Spacing();

		//if (bOvercount)
		//{
		//	ImGui::TextColored(Style::BAD, "Drawn over submitted by %u",
		//		mlDrawn - gpu.meshletsSubmitted);
		//}
		//else
		//{
		//	const float culledFrac =
		//		float(gpu.meshletsSubmitted - mlDrawn) / float(gpu.meshletsSubmitted);

		//	UI::bar(
		//		culledFrac,
		//		fmt::format("culled {:.1f}%", 100.0f * culledFrac).c_str(),
		//		Style::BAR_ASYNC);
		//}

		//{
		//	UI::MetricTable table("MeshletOvercount");
		//	if (table)
		//	{
		//		if (peakOvercount > 0u)
		//		{
		//			UI::metric("Peak over", UI::formatCount(peakOvercount),
		//				(overcountFrames > OVERCOUNT_DECAY_FRAMES) ? Style::BAD : Style::WARN);
		//			UI::metric("Frames", fmt::format("{}", overcountFrames), Style::MUTED);
		//		}
		//		else
		//		{
		//			UI::metric("Peak over", "--", Style::MUTED);
		//			UI::metric("Frames", "--", Style::MUTED);
		//		}
		//	}
		//}

		//ImGui::Spacing();
		UI::separatorText("Cull Info");
		{
			const uint64_t reasonSum =
				uint64_t(gpu.meshletsCulledFrustum) +
				uint64_t(gpu.meshletsCulledCone) +
				uint64_t(gpu.meshletsCulledHiZ);

			UI::MetricTable table("MeshletCullReasons");
			if (table) {
				UI::metric("Frustum", UI::formatCount(gpu.meshletsCulledFrustum));
				UI::metric("Cone", UI::formatCount(gpu.meshletsCulledCone));
				UI::metric("Hi-Z", UI::formatCount(gpu.meshletsCulledHiZ));
				UI::metric("Sum", UI::formatCount(reasonSum), Style::MUTED);
			}
		}

		ImGui::Spacing();
		UI::separatorText("Triangles");

		const uint32_t drawnTris = gpu.meshletTriangles;

		UI::MetricTable table("MeshletTriangles");
		if (!table) return;

		UI::metric("Submitted", fmt::format("{:.2f}M", double(gpu.triangleCount) / 1e6));
		UI::metric("Drawn", fmt::format("{:.2f}M", double(drawnTris) / 1e6),
			(drawnTris > gpu.triangleCount) ? Style::BAD : Style::MUTED);

		if (gpu.triangleCount > 0u && drawnTris <= gpu.triangleCount)
		{
			UI::metric("Reduction", fmt::format(
				"{:.1f}%", 100.0f * (1.0f - float(drawnTris) / float(gpu.triangleCount))));
		}
	}

	static void sectionPassTimings(UIContext& ui)
	{
		Profiler& profiler = *ui.profiler;

		static bool bHighlightAsync = true;
		static bool bSortByCost     = false;
		static bool bShowCpu        = true;

		ImGui::Checkbox("Async##passes", &bHighlightAsync);
		ImGui::SameLine();
		ImGui::Checkbox("Sort##passes", &bSortByCost);
		ImGui::SameLine();
		ImGui::Checkbox("CPU##passes", &bShowCpu);

		const auto& allPassStats = profiler.GetAllPassStats();

		// Index list so sorting never touches the profiler's own storage.
		std::array<uint32_t, RD::PASS_COUNT> order{};
		uint32_t activeCount = 0u;
		float maxGpuMs = 0.0f;

		for (uint32_t passIndex = 0; passIndex < static_cast<uint32_t>(allPassStats.size()); ++passIndex)
		{
			const PassTimingStats& passStats = allPassStats[passIndex];
			if (!passStats.activeLastFrame) continue;

			order[activeCount++] = passIndex;
			maxGpuMs = std::max(maxGpuMs, passGpuMs(passStats));
		}

		if (activeCount == 0u)
		{
			ImGui::TextDisabled("No passes recorded last frame.");
			return;
		}

		if (bSortByCost)
		{
			std::sort(
				order.begin(),
				order.begin() + activeCount,
				[&allPassStats](uint32_t a, uint32_t b) {
					return passGpuMs(allPassStats[a]) > passGpuMs(allPassStats[b]);
				});
		}

		const int columnCount = bShowCpu ? 3 : 2;

		UI::TableScope table(
			"PassTimings",
			columnCount,
			ImGuiTableFlags_SizingStretchProp |
			ImGuiTableFlags_BordersInnerV |
			ImGuiTableFlags_RowBg);

		if (!table) return;

		ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch, bShowCpu ? 0.45f : 0.55f);
		if (bShowCpu) {
			ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_WidthStretch, 0.20f);
		}
		ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthStretch, bShowCpu ? 0.35f : 0.45f);
		ImGui::TableHeadersRow();

		for (uint32_t i = 0; i < activeCount; ++i)
		{
			const RD::Renderer_Pass passID    = static_cast<RD::Renderer_Pass>(order[i]);
			const PassTimingStats&  passStats = allPassStats[order[i]];

			ImGui::TableNextRow();

			if (bHighlightAsync && passStats.asyncQueueLastFrame)
			{
				ImGui::TableSetBgColor(
					ImGuiTableBgTarget_RowBg0,
					ImGui::GetColorU32(Style::ROW_ASYNC));
			}

			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(profiler.GetPassName(passID));

			int column = 1;

			if (bShowCpu)
			{
				ImGui::TableSetColumnIndex(column++);
				if (passStats.cpuMsAverage.IsInitialized())
					UI::textRightAligned(fmt::format("{:.3f}", passCpuMs(passStats)).c_str(), nullptr);
				else
					UI::textRightAligned("--", &Style::MUTED);
			}

			ImGui::TableSetColumnIndex(column);
			if (passStats.gpuMsAverage.IsInitialized())
			{
				const float gpuMs = passGpuMs(passStats);
				const float fraction = (maxGpuMs > 0.0f) ? (gpuMs / maxGpuMs) : 0.0f;

				UI::bar(
					fraction,
					fmt::format("{:.3f}", gpuMs).c_str(),
					passStats.asyncQueueLastFrame ? Style::BAR_ASYNC : Style::BAR_GFX);
			}
			else
			{
				UI::textRightAligned("--", &Style::MUTED);
			}
		}
	}

	static const ControlGroup PROFILER_SECTIONS[] = {
		{ "Frame",        &sectionFrame,        true  },
		{ "GPU",          &sectionGpu,          true  },
		{ "Visibility",   &sectionVisibility,   false },
		{ "Culling",      &sectionMeshlets,     false },
		{ "Pass Timings", &sectionPassTimings,  true  },
		{ "Asset Data",   &sectionAssets,       false },
	};

	// =========================================================================
	// 7. Panels
	// =========================================================================
	static void panelSettingsWindow(UIContext& ui)
	{
		ImGui::SetNextWindowPos(
			ImVec2(Style::WINDOW_PADDING, Style::WINDOW_PADDING), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(
			ImVec2(Editor::SETTINGS_SIZE_X, Editor::SETTINGS_SIZE_Y), ImGuiCond_FirstUseEver);

		UI::WindowScope window(
			"Settings",
			nullptr,
			ImGuiWindowFlags_NoCollapse);

		if (!window) return;

		static Editor::SettingsCategory selectedCategory = Editor::SettingsCategory::Render;

		{
			UI::ChildScope left("SettingsLeft", ImVec2(Style::CATEGORY_LIST_WIDTH, 0.0f), true);
			if (left) {
				drawCategorySelector(selectedCategory);
			}
		}

		ImGui::SameLine();

		{
			UI::ChildScope right("SettingsRight", ImVec2(0.0f, 0.0f), true);
			if (right)
			{
				const int categoryIndex = static_cast<int>(selectedCategory);

				if (categoryIndex >= 0 && categoryIndex < IM_ARRAYSIZE(SETTINGS_CATEGORIES))
				{
					const SettingsCategoryEntry& category = SETTINGS_CATEGORIES[categoryIndex];
					drawGroups(ui, category.groups, category.groupCount);
				}
			}
		}
	}

	static void panelProfilerWindow(UIContext& ui)
	{
		ImGuiIO& io = ImGui::GetIO();

		ImVec2 profilerPos = ImVec2(
			io.DisplaySize.x - Editor::PROFILER_SIZE_X - Style::WINDOW_PADDING,
			Style::WINDOW_PADDING
		);

		ImGui::SetNextWindowPos(profilerPos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(Editor::PROFILER_SIZE_X, Editor::PROFILER_SIZE_Y), ImGuiCond_Always);

		UI::WindowScope window(
			"Profiler",
			nullptr,
			ImGuiWindowFlags_NoCollapse);

		if (!window) return;

		drawGroups(ui, PROFILER_SECTIONS, IM_ARRAYSIZE(PROFILER_SECTIONS));
	}

	static const PanelRegistry& getPanelRegistry()
	{
		static const PanelRegistry registry = []
		{
			PanelRegistry built;

			built.addPanel("SettingsWindow", &panelSettingsWindow,
				[](const UIContext& ui) { return ui.dbg->enableSettings != 0u; });

			built.addPanel("ProfilerWindow", &panelProfilerWindow,
				[](const UIContext& ui) { return ui.dbg->enableProfilerView != 0u; });

			return built;
		}();

		return registry;
	}
}

// =============================================================================
// 8. Editor
// =============================================================================
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

	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; // Prevent ImGui from overriding the cursor
	io.ConfigFlags |= ImGuiConfigFlags_NoKeyboard;
	io.IniFilename = nullptr; // Won't create imgui file

	ImGui_ImplGlfw_InitForVulkan(window, true);

	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = deviceCtx.instance;
	init_info.PhysicalDevice = deviceCtx.physicalDevice;
	init_info.Device = deviceCtx.device;
	init_info.Queue = graphicsQ.GetQueue();
	init_info.DescriptorPool = m_imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.UseDynamicRendering = true;

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

	getPanelRegistry().draw(ui);

	ImGui::Render();
}

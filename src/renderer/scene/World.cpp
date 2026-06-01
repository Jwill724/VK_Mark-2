#include "pch.h"

#include "World.h"
#include "Scene.h"
//#include "DrawPreparation.h"
//#include "Visibility.h"
#include "LightingSystem.h"
#include "../backend/memory/ResourceAllocator.h"
#include "../RendererDefinitions.h"
#include "../../input/UserInput.h"
#include "../backend/memory/BindlessImageTable.h"
#include "../frame/FrameContext.h"
#include "../../profiler/Profiler.h"
//#include "Mesh.h"

static constexpr glm::vec3 DEFAULT_SPAWN { 1.0, 1.0, 1.0 };

namespace RD = RendererDefinitions;

static uint32_t jitter_frame_index = 0u;

namespace World
{
	struct SceneProfileEntry
	{
		RD::InstancingMethod drawType = RD::InstancingMethod::DrawStatic;
		uint32_t instanceCount = 1;
	};

	std::unordered_map<ModelID, SceneProfileEntry> _sceneProfiles
	{
		{ ModelID::Sponza,            {} },
		{ ModelID::Bistro,            {} },
		{ ModelID::MRSpheres,         {} },
		{ ModelID::Duck,              {} },
		{ ModelID::DamagedHelmet,     {} },
		{ ModelID::DragonAttenuation, {} },
		{ ModelID::City,              {} },
		{ ModelID::Structure,         {} },
		{ ModelID::EmissiveTest,      {} },
		{ ModelID::WrathDragon,       {} },
		{ ModelID::Mech,              {} },
		{ ModelID::YellowMech,        {} },
		{ ModelID::Mini,              {} },
	};

	Scene _scene;
	Scene& GetScene() { return _scene; }

	VisibilityState _visState;

	bool _bIsLastFlashlightActive = false;
	bool _bIsFlashlightDirtyAllFrames = false;
	uint32_t _lightStateVersion = 0u;

	bool _bAreAssetsLoaded = false;

	bool SyncGlobalInstancesAndTransforms(
		std::unordered_map<ModelID, SceneProfileEntry>& sceneProfiles,
		std::vector<VirtualInstance>& globalInstances,
		std::vector<glm::mat4>& globalTransforms,
		const double deltaTime);
}

void World::Cleanup()
{
	//_loadedScenes.clear();
	_visState.Cleanup();
	_scene.Shutdown();
}

void World::Init(const BindlessImageTable& imgTable, bool isAssetsLoaded)
{
	uint32_t cookieGoboID = imgTable.GetStaticTexture(RD::Renderer_Texture::CookieGobo).m_bindlessID;
	uint32_t flashlightShadowMapID = imgTable.GetRenderTarget(RD::Renderer_RenderTarget::FlashlightShadowMap).m_bindlessID;
	LightingSystem::_mainFlashLight.Init(flashlightShadowMapID, cookieGoboID);

	_scene.InitScene(DEFAULT_SPAWN);

	const auto& csm = imgTable.GetRenderTarget(RD::Renderer_RenderTarget::DirectionalCSMAtlas);
	_scene.InitCSMInfo(csm.Width(), csm.Height(), csm.m_bindlessID);

	_bAreAssetsLoaded = isAssetsLoaded;
}

void World::UpdateWorldState(
	FrameContext& frameCtx,
	Allocator& allocator,
	Profiler& profiler,
	GLFWwindow* window)
{
	bool bIsTemporalInvalid = false; // Assume clean start each frame

	auto& sceneData = _scene.GetSceneData();
	auto& camera = _scene.GetCamera();

	sceneData.temporal.x = jitter_frame_index++;

	bIsTemporalInvalid = _scene.UpdateCamera(frameCtx.GetCachedExtent(), profiler, window);

	// No scene loaded in
	if (!_bAreAssetsLoaded)
	{
		sceneData.temporal.y = 0u;
		frameCtx.AssignSceneUniform(allocator.AllocateUniform(sceneData), allocator);
		return;
	}

	// =====================
	// RESET TRANSIENT DATA
	// =====================
	frameCtx.ClearDrawData();

	const auto deltaTime = profiler.getStats().deltaSecondsRaw;

	// Light Updates, handle dynamics first
	bool bMainList = false;
	bool bDynamicList = false;
	bool bFlashlightChanged = false;

	// ====================
	// Flashlight updates
	// ====================

	if (UserInput::keyboard.isPressed(UserInput::Keys::F))
	{
		LightingSystem::_mainFlashLight.ToggleFlashLight();
	}

	const bool bIsFlashlightActive = LightingSystem::_mainFlashLight.IsFlashLightActive();

	// Positional changes to dirty light
	bFlashlightChanged = LightingSystem::_mainFlashLight.UpdateFlashLight(
		LightingSystem::_globalLightList,
		camera.GetPosition(),
		camera.GetView(),
		deltaTime,
		UserInput::mouse.rightPressed ? camera.GetDelta() : glm::vec2(0.0f),
		camera.GetView()
	);

	// Light toggle made it dirty
	if (bIsFlashlightActive != _bIsLastFlashlightActive)
	{
		_bIsLastFlashlightActive = bIsFlashlightActive;
		bFlashlightChanged = true;
	}

	if (bFlashlightChanged)
	{
		++_lightStateVersion;
	}

	// Compare current frame versions
	frameCtx.IsFlashlightStateVersionOld(_lightStateVersion);

	// ====================
	// Many lights updates
	// ====================

	bMainList = LightingSystem::UpdateLightList();
	if (LightingSystem::_dynamicLightsEnabled)
	{
		bDynamicList = LightingSystem::UpdateDynamicLightsOrbit(deltaTime);
		frameCtx.MarkDynamicLightTransforms();
	}
	else
	{
		frameCtx.EvaluatePossibleLightUpdateStatus();
	}

	// Static update changes
	frameCtx.EvaluateLightListSizeChanges(LightingSystem::_globalLightList.size());

	bool bIsLightUploadedNeeded = (bMainList || bDynamicList || bFlashlightChanged);

	frameCtx.IsFirstLightsUpload(bIsLightUploadedNeeded);

	if (bIsLightUploadedNeeded) { frameCtx.MarkLightUpload(); }

	// ===================
	// Transform updates
	// ===================

	auto result = SyncGlobalInstancesAndTransforms(
		_sceneProfiles,
		_scene.GetVirtualInstances(),
		_scene.GetTransforms(),
		deltaTime);

	frameCtx.EvaluateTransformsStatus(result);

	bIsTemporalInvalid = _scene.VerifyTransformCount();

	// Screen space contact shadows
	if (profiler.debugToggles.enableSSS)
	{
		_scene.BuildDispatchList();
	}

	// Cascaded shadow map updates
	if (profiler.debugToggles.enableShadows)
	{
		_scene.UpdateCSMInfo();
	}

	// Now the temporal should be known if this frame is safe
	_scene.SetTemporalValue(bIsTemporalInvalid);

	// ================================
	// Scene uniform buffer creation
	// ================================
	frameCtx.AssignSceneUniform(allocator.AllocateUniform(sceneData), allocator);

	// Vulkan requires a buffer created once its defined in used shader, even if that buffer isn't actually used.
	frameCtx.AssignCSMUniform(allocator.AllocateUniform(_scene.GetCSMData()), allocator);
}

//void World::UpdateDrawData(
//	FrameContext& frameCtx,
//	const std::vector<Mesh>& meshes,
//	const std::vector<MeshLODs>& meshLODs,
//	const Profiler& profiler)
//{
//	const auto& debug = profiler.debugToggles;
//
//	auto visResult = Visibility::syncFromGlobalInstances(
//		_visState,
//		_scene.GetVirtualInstances(),
//		_loadedScenes,
//		meshes,
//		_scene.GetTransforms());
//
//	frameCtx.SetVisibilityResult(visResult);
//
//	Visibility::ApplySyncResult(_visState, frameCtx.GetVisibilitySyncResult());
//
//	// CPU CULLING
//	Visibility::cullBVHCollect(
//		_visState,
//		_currentFrustum,
//		frameCtx.m_visibleInstances,
//		_visibleWorldAABBs);
//
//	if (!frameCtx.m_visibleInstances.empty())
//	{
//		frameCtx.m_visibleCount = static_cast<uint32_t>(frameCtx.m_visibleInstances.size());
//
//		if (debug.enableShadows)
//		{
//			frameCtx.m_visibleShadowCasters.reserve(std::max(1024u, frameCtx.m_visibleCount * 2u));
//
//			AABB visibleReceiverWS = ComputeVisibleReceiverAABB(_visibleWorldAABBs);
//			const glm::vec3 centerWS = 0.5f * (visibleReceiverWS.vmin + visibleReceiverWS.vmax);
//			const glm::vec3 extentWS = 0.5f * (visibleReceiverWS.vmax - visibleReceiverWS.vmin);
//
//			for (uint32_t cascadeIndex = 0; cascadeIndex < MAX_SHADOW_CASCADES; ++cascadeIndex) {
//				IndirectDrawRange& cascadeRange = frameCtx.m_csmDrawRange[cascadeIndex];
//				cascadeRange.firstInstance = static_cast<uint32_t>(frameCtx.m_visibleShadowCasters.size());
//
//				// Transform to light space
//				glm::vec3 centerLS = glm::vec3(_cascadeLightViews[cascadeIndex] * glm::vec4(centerWS, 1.0f));
//				glm::mat3 absLightMat = glm::mat3(
//					glm::abs(_cascadeLightViews[cascadeIndex][0]),
//					glm::abs(_cascadeLightViews[cascadeIndex][1]),
//					glm::abs(_cascadeLightViews[cascadeIndex][2]));
//
//				glm::vec3 extentLS = absLightMat * extentWS;
//
//				glm::vec3 receiverLSMin = centerLS - extentLS;
//				glm::vec3 receiverLSMax = centerLS + extentLS;
//
//				//// Extend toward the light (higher Z)
//				//receiverLSMax.z += _shadowControl.maxCasterDistance[cascadeIndex];
//
//				//// Small safety padding
//				//receiverLSMin.x -= _shadowControl.xyPadding;
//				//receiverLSMin.y -= _shadowControl.xyPadding;
//				//receiverLSMax.x += _shadowControl.xyPadding;
//				//receiverLSMax.y += _shadowControl.xyPadding;
//
//				Visibility::cullBVHCollectShadowCastersReceivers(
//					cascadeIndex,
//					_visState,
//					_cascadeFrustums[cascadeIndex],
//					_cascadeLightViews[cascadeIndex],
//					receiverLSMin,
//					receiverLSMax,
//					frameCtx.m_visibleShadowCasters,
//					gpuResources.GetMaterialFlagsByID()
//				);
//
//				cascadeRange.visibleCount =
//					static_cast<uint32_t>(frameCtx.m_visibleShadowCasters.size()) - cascadeRange.firstInstance;
//
//				ASSERT(cascadeRange.firstInstance + cascadeRange.visibleCount <= frameCtx.m_visibleShadowCasters.size());
//			}
//		}
//
//		if (LightingSystem::_mainFlashLight.IsFlashLightOn()) {
//			frameCtx.m_visibleShadowCasters.reserve(frameCtx.m_visibleShadowCasters.size() + frameCtx.m_visibleCount);
//
//			frameCtx.m_flashlightShadowCasterRange.firstInstance =
//				static_cast<uint32_t>(frameCtx.m_visibleShadowCasters.size());
//
//			Visibility::cullBVHCollectShadowCasters(
//				_visState,
//				LightingSystem::_mainFlashLight.frustum,
//				frameCtx.m_visibleShadowCasters,
//				gpuResources.GetMaterialFlagsByID(),
//				false
//			);
//
//			frameCtx.m_flashlightShadowCasterRange.visibleCount =
//				static_cast<uint32_t>(frameCtx.m_visibleShadowCasters.size()) - frameCtx.m_flashlightShadowCasterRange.firstInstance;
//
//			ASSERT(frameCtx.m_flashlightShadowCasterRange.firstInstance +
//				frameCtx.m_flashlightShadowCasterRange.visibleCount <= frameCtx.m_visibleShadowCasters.size());
//		}
//
//		DrawPreparation::buildAndSortIndirectDraws(
//			frameCtx,
//			meshes.meshData,
//			meshes.meshLODs,
//			_visibleWorldAABBs,
//			_sceneData.cameraPos,
//			_curCamProjUnjittered,
//			debug);
//	}
//
//	DrawPreparation::uploadGPUBuffersForFrame(
//		frameCtx,
//		gpuResources,
//		_globalTransforms,
//		LightingSystem::_globalLightList,
//		Backend::GetTransferQueue(),
//		bool(_sceneData.temporal.y),
//		Engine::GetProfiler().isGPUAccelOn());
//}

// ===============================
// Hard coded transform code sucks

static glm::mat4 makeGridTransform3D(uint32_t index, uint32_t count, float spacing)
{
	ASSERT(count > 0);

	const float cubeRoot = std::cbrt(static_cast<float>(count));
	const uint32_t gridDim = glm::max(1u, static_cast<uint32_t>(std::ceil(cubeRoot)));

	const uint32_t layerSize = gridDim * gridDim;

	const uint32_t y = index / layerSize;
	const uint32_t rem = index - y * layerSize;

	const uint32_t z = rem / gridDim;
	const uint32_t x = rem - z * gridDim;

	const glm::vec3 translation(
		static_cast<float>(x) * spacing,
		static_cast<float>(y) * spacing,
		static_cast<float>(z) * spacing);

	return glm::translate(glm::mat4(1.0f), translation);
}

static bool braindeadhack = false;
bool World::SyncGlobalInstancesAndTransforms(
	std::unordered_map<ModelID, SceneProfileEntry>& sceneProfiles,
	std::vector<VirtualInstance>& globalInstances,
	std::vector<glm::mat4>& globalTransforms,
	const double deltaTime)
{
	bool transformsUpdated = false;

	for (auto& inst : globalInstances) {
		ModelID sid = static_cast<ModelID>(inst.sceneID);
		SceneProfileEntry& profile = sceneProfiles.at(sid);

		if (profile.instanceCount == 1) {
			if (profile.drawType == RD::InstancingMethod::DrawStatic) {
				// TODO: Create a way to modify transforms at runtime
				if (!braindeadhack && sid == ModelID::DamagedHelmet) {
					glm::mat4& M = globalTransforms[inst.firstTransform];
					// Turn helmet for bake
					M = glm::rotate(M, glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f));

					inst.baseTransform = M;
					transformsUpdated = true;
					braindeadhack = true;
				}
				inst.drawType = profile.drawType;
				continue; // transforms already baked into static
			}
			if (profile.drawType == RD::InstancingMethod::DrawDynamic) {
				inst.drawType = profile.drawType;

				constexpr float spinSpeedRadiansPerSecond = glm::radians(30.0f);
				const float deltaSecondsFloat = static_cast<float>(deltaTime);

				inst.spinAngleRadians += spinSpeedRadiansPerSecond * deltaSecondsFloat;

				const glm::mat4& baseTransform = inst.baseTransform;
				const glm::vec3 pivot = glm::vec3(baseTransform[3]);
				inst.modelOffset = glm::vec3(0.0f, 2.5f, 0.0f);

				const glm::mat4 newTransform =
					glm::translate(glm::mat4(1.0f), pivot) *
					glm::rotate(glm::mat4(1.0f), inst.spinAngleRadians, glm::vec3(0.0f, 1.0f, 0.0f)) *
					glm::translate(glm::mat4(1.0f), -pivot) *
					glm::translate(glm::mat4(1.0f), inst.modelOffset) *
					baseTransform;

				globalTransforms[inst.firstTransform] = newTransform;

				transformsUpdated = true;
				continue;
			}
		//	if (profile.drawType == InstancingMethod::DrawDynamic) {
		//		inst.drawType = profile.drawType;

		//		const float deltaSecondsFloat = static_cast<float>(deltaTime);

		//		// Advance a phase accumulator (store this per instance like you do spinAngleRadians)
		//		const float moveSpeedRadiansPerSecond = 1.5f; // tweak
		//		inst.movePhaseRadians += moveSpeedRadiansPerSecond * deltaSecondsFloat;

		//		// Line direction and amplitude
		//		const glm::vec3 lineDir = glm::normalize(glm::vec3(1.0f, 0.0f, 0.0f)); // X axis
		//		const float amplitude = 2.0f; // world units

		//		const float t = std::sin(inst.movePhaseRadians);
		//		const glm::vec3 moveOffset = lineDir * (t * amplitude);

		//		const glm::mat4& baseTransform = inst.baseTransform;

		//		glm::mat4 newTransform = baseTransform;
		//		newTransform[3] = baseTransform[3] + glm::vec4(moveOffset, 0.0f);

		//		globalTransforms[inst.firstTransform] = newTransform;

		//		transformsUpdated = true;
		//		continue;
		//	}
		}

		// TODO: ADD SUPPORT FOR MULTI-DYNAMIC

		// Defined from copy values append list or decrease list
		// on first run this will always be an append
		if (profile.drawType == RD::InstancingMethod::DrawMultiStatic || profile.instanceCount > 1) {
			inst.drawType = profile.drawType;

			const uint32_t desiredUsedCopies = std::max(1u, profile.instanceCount);

			const uint32_t desiredCapacityCopies = desiredUsedCopies;

			if (inst.usedCopies == desiredUsedCopies && inst.capacityCopies >= desiredCapacityCopies) {
				continue;
			}

			const uint32_t transformsPerCopy = inst.transformCount;
			ASSERT(transformsPerCopy > 0);

			const uint32_t oldCapacityCopies = inst.capacityCopies;
			const uint32_t oldSlabTransformCount = oldCapacityCopies * transformsPerCopy;
			const uint32_t oldSlabBegin = inst.firstTransform;
			const uint32_t oldSlabEnd = oldSlabBegin + oldSlabTransformCount;

			// We can only append in-place if this slab currently ends at the end of the global array.
			const bool slabIsAtEnd = (oldSlabEnd == globalTransforms.size());

			if (desiredCapacityCopies > oldCapacityCopies) {
				const glm::mat4 baseTransform = globalTransforms[oldSlabBegin];

				//fmt::print("[syncGI] multistatic: oldCap={} newCap={} firstT={} tfCount={} slabEnd={} tfSize(before)={}\n",
				//	oldCapacityCopies,
				//	desiredCapacityCopies,
				//	inst.firstTransform,
				//	transformsPerCopy,
				//	oldSlabEnd,
				//	globalTransforms.size());

				if (!slabIsAtEnd) {
					// Relocate this slab to the end to preserve the "contiguous slab" invariant.
					const uint32_t newFirstTransform = static_cast<uint32_t>(globalTransforms.size());

					// Copy old slab transforms.
					for (uint32_t i = 0; i < oldSlabTransformCount; ++i) {
						globalTransforms.push_back(globalTransforms[static_cast<size_t>(oldSlabBegin + i)]);
					}

					inst.firstTransform = newFirstTransform;
				}

				// Append transforms for new copies (copy indices [oldCap .. newCap)).
				for (uint32_t copyIndex = oldCapacityCopies; copyIndex < desiredCapacityCopies; ++copyIndex) {
					glm::mat4 offset = makeGridTransform3D(copyIndex, desiredCapacityCopies, 5.0f);

					for (uint32_t slot = 0; slot < transformsPerCopy; ++slot) {
						const uint32_t baseSlotIndex = inst.firstTransform + slot; // copy 0, slot N
						const glm::mat4 slotBaseTransform = globalTransforms[baseSlotIndex];

						globalTransforms.push_back(offset * slotBaseTransform);
					}
				}

				//fmt::print("[syncGI] tfSize(after)={} firstT={} relocated={}\n",
				//	globalTransforms.size(),
				//	inst.firstTransform,
				//	(!slabIsAtEnd));

				inst.capacityCopies = desiredCapacityCopies;
				transformsUpdated = true;
			}

			// Change active copies (visibility will rebuild/activate).
			inst.usedCopies = desiredUsedCopies;
		}
	}

	return transformsUpdated;
}

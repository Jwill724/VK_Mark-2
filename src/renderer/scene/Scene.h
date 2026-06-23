#pragma once

#include "Bounds.h"
#include "ResourceTypes.h"
#include "../../input/Camera.h"

struct Extents2D;
class Profiler;
struct GLFWwindow;

class Scene
{
public:
	SceneInfo& GetSceneData() { return m_sceneInfo; }
	const SceneInfo& GetSceneData() const { return m_sceneInfo; }
	DirectionalCSMInfo& GetCSMData() { return m_csmInfo; }
	const DirectionalCSMInfo& GetCSMData() const { return m_csmInfo; }
	Camera& GetCamera() { return m_camera; }
	const Camera& GetCamera() const { return m_camera; }

	ShadowControl& GetShadowControls() { return m_shadowControl; }
	const ShadowControl& GetShadowControls() const { return m_shadowControl; }

	void InitScene(glm::vec3 spawn);
	void InitCSMInfo(uint32_t atlasWidth, uint32_t atlasHeight, uint32_t bindlessID);

	void Shutdown()
	{
		m_transforms.clear();
		m_virtualInstances.clear();
	}

	const Frustum& GetCascadeFrustum(uint32_t index) const { return m_cascadeFrustums[index]; }
	glm::mat4& GetCascadeLightView(uint32_t index) { return m_cascadeLightViews[index]; }
	const glm::mat4& GetCascadeLightView(uint32_t index) const { return m_cascadeLightViews[index]; }

	const Frustum& GetFrustum() const { return m_frustum; }

	const glm::mat4& GetCurrentProjUnjittered() const { return m_curCamProjUnjittered; }

	bool UpdateCamera(
		Extents2D drawExtent,
		Profiler& profiler,
		GLFWwindow* window);

	void UpdateCSMInfo();

	void SetTemporalValue(bool result)
	{
		m_sceneInfo.temporal.y = result ? 0u : 1u;
	}

	bool GetTemporalResult() const noexcept
	{
		return static_cast<bool>(m_sceneInfo.temporal.y);
	}

	bool TemporalResult() const { return static_cast<bool>(m_sceneInfo.temporal.y); }

	// Returns normalized
	glm::vec3 GetLightDir();

	std::vector<VirtualInstance>& GetVirtualInstances() { return m_virtualInstances; }
	std::vector<glm::mat4>& GetTransforms() { return m_transforms; }
	std::vector<AABB>& GetVisibleWorldAABBs() { return m_visibleWorldAABBs; }

	// Verifys for temporal
	bool VerifyTransformCount()
	{
		return (static_cast<uint32_t>(m_transforms.size()) != m_recentTransformCount);
	}

	void BuildDispatchList(const int waveSize = 64);
	const DispatchList& GetDispatchList() const { return m_dispatchList; }

	void ShouldUpdateCascadeSplits() { m_bShouldSplitsUpdate = true; }
private:
	SceneInfo m_sceneInfo;
	DirectionalCSMInfo m_csmInfo;
	Camera m_camera;
	Frustum m_frustum;

	void UpdateFrustum(const glm::mat4& viewProj) { m_frustum.ExtractNew(viewProj); }

	Frustum m_cascadeFrustums[RD::MAX_SHADOW_CASCADES];
	glm::mat4 m_cascadeLightViews[RD::MAX_SHADOW_CASCADES];
	glm::mat4 m_cascadeLightProjs[RD::MAX_SHADOW_CASCADES];

	float m_csmAtlasTileRes = 0.0f;

	float m_shadowFar = 1000.0f;
	bool m_bShouldSplitsUpdate = true;

	glm::mat4 m_curCamView;
	glm::mat4 m_curCamProj;

	glm::mat4 m_curCamProjUnjittered = glm::mat4(1.0f);

	glm::mat4 m_curCamProjJittered = glm::mat4(1.0f);

	glm::mat4 m_lastViewProjUnjittered = glm::mat4(1.0f);
	glm::mat4 m_lastViewProjJittered = glm::mat4(1.0f);

	glm::vec2 m_currentJitterNDC = glm::vec2(0.0f);
	glm::vec2 m_previousJitterNDC = glm::vec2(0.0f);

	glm::mat4 m_lastView = glm::mat4(1.0f);

	std::vector<VirtualInstance> m_virtualInstances;
	std::vector<glm::mat4> m_transforms;
	uint32_t m_recentTransformCount = 0;

	std::vector<AABB> m_visibleWorldAABBs;

	ShadowControl m_shadowControl;

	// Using for bend studios contact shadows
	DispatchList m_dispatchList;
};

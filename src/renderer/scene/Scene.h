#pragma once

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

	const std::vector<VirtualInstance>& GetVirtualInstances() const { return m_virtualInstances; }
	std::vector<VirtualInstance>& GetVirtualInstances() { return m_virtualInstances; }

	const std::vector<glm::mat4>& GetStaticTransforms()  const { return m_staticTransforms; }
	std::vector<glm::mat4>&       GetStaticTransforms()        { return m_staticTransforms; }

	const std::vector<glm::mat4>& GetDynamicTransforms() const { return m_dynamicTransforms; }
	std::vector<glm::mat4>&       GetDynamicTransforms()       { return m_dynamicTransforms; }

	const std::vector<glm::mat4>& GetMotionMatrices()    const { return m_motionMatrices; }

	std::vector<glm::mat4>& GetTransformPool(bool bDynamic)
	{
		return bDynamic ? m_dynamicTransforms : m_staticTransforms;
	}

	const std::vector<glm::mat4>& GetTransformPool(bool bDynamic) const
	{
		return bDynamic ? m_dynamicTransforms : m_staticTransforms;
	}

	void MarkStaticTransformsDirty(uint32_t offset, uint32_t count)
	{
		if (count == 0u) return;

		if (m_staticDirty.count == 0u)
		{
			m_staticDirty = { offset, count };
			return;
		}

		const uint32_t newEnd = std::max(m_staticDirty.offset + m_staticDirty.count, offset + count);
		m_staticDirty.offset  = std::min(m_staticDirty.offset, offset);
		m_staticDirty.count   = newEnd - m_staticDirty.offset;
	}

	const DirtyRange& GetStaticDirtyRange() const noexcept { return m_staticDirty; }
	bool IsStaticTransformsDirty()          const noexcept { return m_staticDirty.count > 0u; }
	void ClearStaticTransformsDirty()             noexcept { m_staticDirty = {}; }

	void BuildMotionMatrices(bool bMotionNeeded, bool bTemporalValid);

	bool VerifyTransformCount()
	{
		const uint32_t current =
			static_cast<uint32_t>(m_staticTransforms.size() + m_dynamicTransforms.size());
		const bool changed = (current != m_recentTransformCount);
		m_recentTransformCount = current;
		return changed;
	}

	void Shutdown()
	{
		m_staticTransforms.clear();
		m_dynamicTransforms.clear();
		m_prevDynamicTransforms.clear();
		m_motionMatrices.clear();
		m_virtualInstances.clear();
		m_staticDirty = {};
	}

	void BuildDispatchList(const int waveSize = 64);
	const DispatchList& GetDispatchList() const { return m_dispatchList; }

	void ShouldUpdateCascadeSplits() { m_bShouldSplitsUpdate = true; }
private:
	SceneInfo m_sceneInfo;
	DirectionalCSMInfo m_csmInfo;
	Camera m_camera;

	glm::mat4 m_cascadeLightProjs[RD::MAX_SHADOW_CASCADES];

	float m_csmAtlasTileRes = 0.0f;

	float m_shadowFar = 1000.0f;
	bool m_bShouldSplitsUpdate = true;

	glm::mat4 m_curCamView;
	glm::mat4 m_curCamProj;

	glm::mat4 m_curCamProjUnjittered = glm::mat4(1.0f);

	glm::mat4 m_curCamProjJittered = glm::mat4(1.0f);

	glm::mat4 m_lastViewProjUnjittered = glm::mat4(1.0f);
	//glm::mat4 m_lastViewProjJittered = glm::mat4(1.0f);

	glm::vec2 m_currentJitterNDC = glm::vec2(0.0f);
	glm::vec2 m_previousJitterNDC = glm::vec2(0.0f);

	glm::mat4 m_lastView = glm::mat4(1.0f);

	DirtyRange m_staticDirty{};

	std::vector<VirtualInstance> m_virtualInstances;

	std::vector<glm::mat4> m_staticTransforms;
	std::vector<glm::mat4> m_dynamicTransforms;
	std::vector<glm::mat4> m_prevDynamicTransforms;
	std::vector<glm::mat4> m_motionMatrices;

	uint32_t m_recentTransformCount = 0;

	uint32_t m_lastAaMode = UINT32_MAX;

	ShadowControl m_shadowControl;

	// Using for bend studios contact shadows
	DispatchList m_dispatchList;
};

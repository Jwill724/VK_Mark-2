#pragma once

#include "common/ResourceTypes.h"
#include "common/EngineTypes.h"

enum class SceneID : uint8_t {
	Sponza,
	Bistro,
	MRSpheres,
	Duck,
	DamagedHelmet,
	DragonAttenuation,
	City,
	Structure,
	EmissiveTest,
	Count
};

// User view and control over scene instance data
struct SceneProfileEntry {
	std::string name;
	DrawType drawType;
	uint32_t instanceCount;   // total active instances
	uint32_t reservedCopies;  // capacity
	uint32_t usedCopies;      // currently realized
};

// Defines node transforms for each gltf model
namespace SceneGraph {
	static const std::unordered_map<SceneID, std::string> SceneNames {
		{ SceneID::Sponza, "Sponza" },
		{ SceneID::Bistro, "Bistro" },
		{ SceneID::MRSpheres, "MRSpheres" },
		{ SceneID::Duck, "Duck" },
		{ SceneID::DamagedHelmet, "DamagedHelmet" },
		{ SceneID::DragonAttenuation, "Dragon" },
		{ SceneID::City, "City" },
		{ SceneID::Structure, "Structure" },
		{ SceneID::EmissiveTest, "EmissiveTest" },
	};

	static const std::unordered_map<std::string, SceneID> SceneIDs {
		{ "Sponza", SceneID::Sponza },
		{ "Bistro", SceneID::Bistro },
		{ "MRSpheres", SceneID::MRSpheres },
		{ "Duck", SceneID::Duck },
		{ "DamagedHelmet", SceneID::DamagedHelmet },
		{ "Dragon", SceneID::DragonAttenuation },
		{ "City", SceneID::City },
		{ "Structure", SceneID::Structure },
		{ "EmissiveTest", SceneID::EmissiveTest },
	};

	// ====== Scene Graph Node Base ======
	struct Node {
		std::weak_ptr<Node> parent;
		std::vector<std::shared_ptr<Node>> children;

		glm::mat4 localTransform{ 1.0f };
		glm::mat4 worldTransform{ 1.0f };

		void refreshTransform(const glm::mat4& parentMatrix) {
			worldTransform = parentMatrix * localTransform;
			for (auto& c : children) {
				if (c) c->refreshTransform(worldTransform);
			}
		}
	};

	void buildSceneGraph(
		ThreadContext& threadCtx,
		std::vector<GlobalInstance>& globalInstances,
		std::vector<glm::mat4>& globalTransforms,
		ModelDataCounts& modelDataCounts);
}
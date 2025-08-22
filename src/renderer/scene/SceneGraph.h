#pragma once

#include "common/ResourceTypes.h"
#include "common/EngineTypes.h"

enum class SceneID : uint8_t {
	Sponza,
	MRSpheres,
	Cube,
	DamagedHelmet,
	DragonAttenuation,
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
		{ SceneID::MRSpheres, "MRSpheres" },
		{ SceneID::Cube, "Cube" },
		{ SceneID::DamagedHelmet, "DamagedHelmet" },
		{ SceneID::DragonAttenuation, "Dragon" },
	};

	static const std::unordered_map<std::string, SceneID> SceneIDs {
		{ "Sponza", SceneID::Sponza },
		{ "MRSpheres", SceneID::MRSpheres },
		{ "Cube", SceneID::Cube },
		{ "DamagedHelmet", SceneID::DamagedHelmet },
		{ "Dragon", SceneID::DragonAttenuation },
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
		std::vector<glm::mat4>& globalTransforms);
}
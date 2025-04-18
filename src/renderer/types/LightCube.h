#pragma once

#include "common/Vk_Types.h"

struct LightCube {
	glm::vec3 position;
	glm::mat4 modelMatrix;
	bool isRotating = false;
	float rotationTime = 0.f;
	bool lightOn = true;

	void rotateCube();
};
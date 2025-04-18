#pragma once

#include "UserInput.h"

struct Camera {
	glm::vec3 velocity;
	glm::vec3 position;
	// vertical rotation
	float pitch{ 0.f };
	// horizontal rotation
	float yaw{ 0.f };

	glm::vec3 currentView;

	glm::mat4 getViewMatrix();
	glm::mat4 getRotationMatrix() const;

	void processInput(GLFWwindow* window, float dt);

	void update(GLFWwindow* window, float& lastTime);

	void reset();
};
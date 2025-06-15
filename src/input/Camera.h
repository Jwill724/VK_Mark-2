#pragma once

#include "UserInput.h"

struct Camera {
	glm::vec3 _velocity;
	glm::vec3 _position;
	// vertical rotation
	float _pitch{ 0.f };
	// horizontal rotation
	float _yaw{ 0.f };

	glm::vec3 _currentView;

	glm::mat4 getViewMatrix() const;
	glm::mat4 getRotationMatrix() const;

	void processInput(GLFWwindow* window, float dt);

	void update(GLFWwindow* window, const float deltaTime);

	void reset();
};
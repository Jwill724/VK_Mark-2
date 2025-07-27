#pragma once

#include "UserInput.h"
#include "profiler/Profiler.h"

struct Camera {
	glm::vec3 _velocity;
	glm::vec3 _position;
	// vertical rotation
	float _pitch{ 0.0f };
	// horizontal rotation
	float _yaw{ 0.0f };

	glm::vec3 _currentView;

	glm::mat4 getViewMatrix() const;
	glm::mat4 getRotationMatrix() const;

	void processInput(GLFWwindow* window, Profiler& profiler);

	void reset();
};
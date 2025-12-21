#pragma once

#include "UserInput.h"
#include "engine/platform/profiler/Profiler.h"

struct Camera {
	glm::vec3 _velocity = glm::vec3(0.0f);
	glm::vec3 _position = glm::vec3(0.0f);
	// vertical rotation
	float _pitch{ 0.0f };
	// horizontal rotation
	float _yaw{ 0.0f };

	float _fovY = 0.0f;
	float _nearClip = 0.0f;
	float _farClip = 0.0f;

	glm::vec3 _currentView = glm::vec3(0.0f);

	glm::mat4 getViewMatrix() const;
	glm::mat4 getRotationMatrix() const;

	void processInput(GLFWwindow* window, Profiler& profiler, bool& isTemporalInvalid);

	void reset();
};
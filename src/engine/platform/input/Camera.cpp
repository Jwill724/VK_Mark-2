#include "pch.h"

#include "Camera.h"
#include "engine/platform/profiler/EditorImgui.h"

void Camera::processInput(GLFWwindow* window, Profiler& profiler, bool& isTemporalInvalid) {
	using namespace UserInput;

	updateLocalInput(window);

	// Note: If one imgui window is active and a mouse click on that window is occurring,
	// then if the window is closed, that mouse click stays stuck until the imgui window is reopened.
	// Same behavior as movement keys becoming sticky with window resizing/stalls.
	// This is likely an event queue issue.

	auto& debug = profiler.debugToggles;
	// debug toggle settings
	if (keyboard.isPressed(GLFW_KEY_TAB)) debug.enableSettings = 1u - debug.enableSettings;
	if (keyboard.isPressed(GLFW_KEY_P)) debug.enableStats = 1u - debug.enableStats;

	// Mouse rotation, imgui can be properly used with free cam
	if (!ImGui::GetIO().WantCaptureMouse && mouse.leftPressed) {
		constexpr float sensitivity = 30.0f;
		_yaw -= mouse.delta.x * sensitivity;
		_pitch += mouse.delta.y * sensitivity;
		constexpr float maxPitch = 89.0f;
		_pitch = std::clamp(_pitch, -maxPitch, maxPitch);
	}

	// TODO: movement is slow asf in space station model, due to model units being too large so
	// some scaling factor will need to be added for this particular model
	float baseSpeed = keyboard.isHeld(GLFW_KEY_LEFT_SHIFT) ? 20.0f : 8.0f;
	float moveSpeed = baseSpeed * profiler.getStats().deltaTime.get();

	const float radPitch = glm::radians(_pitch);
	const float radYaw = glm::radians(_yaw);

	glm::vec3 front = glm::normalize(glm::vec3(
		cos(radPitch) * cos(radYaw),
		sin(radPitch),
		cos(radPitch) * sin(radYaw)
	));

	_currentView = front;

	glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
	glm::vec3 up = glm::normalize(glm::cross(right, front));

	// for world orientation relative to the camera movements
	glm::vec3 upWorld(0.0f, 1.0f, 0.0f);
	glm::vec3 flatFoward = glm::normalize(glm::vec3(_currentView.x, 0.0f, _currentView.z));
	glm::vec3 rightFoward = glm::normalize(glm::vec3(right.x, 0.0f, right.z));

	glm::vec3 horiz(0.0f);
	glm::vec3 vert(0.0f);

	if (keyboard.isHeld(GLFW_KEY_W)) { horiz += flatFoward; }
	if (keyboard.isHeld(GLFW_KEY_S)) { horiz -= flatFoward; }
	if (keyboard.isHeld(GLFW_KEY_A)) { horiz -= rightFoward; }
	if (keyboard.isHeld(GLFW_KEY_D)) { horiz += rightFoward; }
	if (keyboard.isHeld(GLFW_KEY_SPACE)) { vert += up * upWorld; }
	if (keyboard.isHeld(GLFW_KEY_LEFT_CONTROL)) { vert -= up * upWorld;}

	if (glm::length(horiz) > 0.0f) horiz = glm::normalize(horiz);
	if (glm::length(vert) > 0.0f) vert = glm::normalize(vert);

	// scale speed on whole axis while frame independent
	_velocity = (horiz + vert) * moveSpeed;

	if (keyboard.isPressed(GLFW_KEY_R)) {
		reset();
		isTemporalInvalid = true;
	}

	_position += _velocity;
}

glm::mat4 Camera::getViewMatrix() const {
	return glm::lookAt(_position, _position + _currentView, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::getRotationMatrix() const {
	glm::quat pitchRotation = glm::angleAxis(glm::radians(_pitch), glm::vec3{ 1.0f, 0.0f, 0.0f });
	glm::quat yawRotation = glm::angleAxis(glm::radians(_yaw), glm::vec3{ 0.0f, 1.0f, 0.0f });

	glm::quat orientation = yawRotation * pitchRotation;
	return glm::toMat4(orientation);
}

void Camera::reset() {
	_position = SPAWNPOINT;
	_pitch = 0.0f;
	_yaw = -90.0f;
}
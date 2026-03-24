#include "pch.h"

#include "Camera.h"
#include "engine/platform/profiler/EditorImgui.h"

void Camera::processInput(GLFWwindow* window, Profiler& profiler, bool& isTemporalInvalid) {
	using namespace UserInput;

	updateLocalInput(window);

	_delta = mouse.delta;

	auto& debug = profiler.debugToggles;
	if (mouse.rightPressed) {
		_yaw -= mouse.delta.x * _sensitivity;
		_pitch += mouse.delta.y * _sensitivity;
		constexpr float maxPitch = 89.0f;
		_pitch = std::clamp(_pitch, -maxPitch, maxPitch);
	}

	const float dt = profiler.getStats().deltaSecondsRaw;

	float baseSpeed = keyboard.isHeld(GLFW_KEY_LEFT_SHIFT) ? _maxSpeed : _minSpeed;
	float moveSpeed = baseSpeed * dt;

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

	if (glm::length(horiz) > 0.0f) { horiz = glm::normalize(horiz); }
	if (glm::length(vert) > 0.0f) { vert = glm::normalize(vert); }

	// Build desired direction
	glm::vec3 desiredDir = horiz + vert;
	float inputMag = glm::length(desiredDir);

	if (inputMag > 0.0f) {
		desiredDir = glm::normalize(desiredDir);

		inputMag = inputMag * inputMag;

		float targetSpeed = baseSpeed * inputMag;

		// Project current velocity onto desired direction
		float currentSpeed = glm::dot(_velocity, desiredDir);
		float addSpeed = targetSpeed - currentSpeed;

		if (addSpeed > 0.0f) {
			float accelSpeed = _acceleration * dt * baseSpeed;
			accelSpeed = std::min(accelSpeed, addSpeed);
			_velocity += desiredDir * accelSpeed;
		}
	}

	// Friction
	float speed = glm::length(_velocity);
	if (speed > 0.0f) {
		float drop = speed * _damping * dt;
		float newSpeed = std::max(speed - drop, 0.0f);
		_velocity *= (newSpeed / speed);
	}

	_position += _velocity * dt;

	if (keyboard.isPressed(GLFW_KEY_R)) {
		reset();
		isTemporalInvalid = true;
	}

	if (keyboard.isPressed(GLFW_KEY_TAB)) {
		debug.enableSettings = 1u - debug.enableSettings;
	}

	if (keyboard.isPressed(GLFW_KEY_P)) {
		debug.enableProfilerView = 1u - debug.enableProfilerView;
	}
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

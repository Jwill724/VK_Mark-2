#include "pch.h"

#include "Camera.h"
#include "profiler/EditorImgui.h"
#include "renderer/RenderScene.h"

void Camera::update(GLFWwindow* window, const float deltaTime) {
	processInput(window, deltaTime);

	_position += _velocity;
}

void Camera::processInput(GLFWwindow* window, float dt) {
	UserInput::updateLocalInput(window, true, true);

	// TODO: movement is slow asf in space station model, due to model units being too large so
	// some scaling factor will need to be added for this particular model
	float baseSpeed = UserInput::keyboard.isPressed(GLFW_KEY_LEFT_SHIFT) ? 15.f : 5.f;
	float moveSpeed = baseSpeed * dt;

	// Mouse rotation, imgui can be properly used with free cam
	if (!ImGui::GetIO().WantCaptureMouse && UserInput::mouse.leftPressed) {
		float sensitivity = 30.f;
		_yaw -= UserInput::mouse.delta.x * sensitivity;
		_pitch += UserInput::mouse.delta.y * sensitivity;
		_pitch = std::clamp(_pitch, -89.f, 89.f);
	}

	float radPitch = glm::radians(_pitch);
	float radYaw = glm::radians(_yaw);

	glm::vec3 front = glm::normalize(glm::vec3(
		cos(radPitch) * cos(radYaw),
		sin(radPitch),
		cos(radPitch) * sin(radYaw)
	));

	_currentView = front;

	glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.f, 1.f, 0.f)));
	glm::vec3 up = glm::normalize(glm::cross(right, front));

	// for world orientation relative to the camera movements
	glm::vec3 upWorld(0.f, 1.f, 0.f);
	glm::vec3 flatFoward = glm::normalize(glm::vec3(_currentView.x, 0.f, _currentView.z));
	glm::vec3 rightFoward = glm::normalize(glm::vec3(right.x, 0.f, right.z));


	glm::vec3 horiz(0.f);
	if (UserInput::keyboard.isPressed(GLFW_KEY_W)) horiz += flatFoward;
	if (UserInput::keyboard.isPressed(GLFW_KEY_S)) horiz -= flatFoward;
	if (UserInput::keyboard.isPressed(GLFW_KEY_A)) horiz -= rightFoward;
	if (UserInput::keyboard.isPressed(GLFW_KEY_D)) horiz += rightFoward;

	glm::vec3 vert(0.f);
	if (UserInput::keyboard.isPressed(GLFW_KEY_SPACE)) vert += up * upWorld;
	if (UserInput::keyboard.isPressed(GLFW_KEY_LEFT_CONTROL)) vert -= up * upWorld;


	if (glm::length(horiz) > 0.f) horiz = glm::normalize(horiz);
	if (glm::length(vert) > 0.f) vert = glm::normalize(vert);

	// scale speed on whole axis while frame independent
	float speedBoost = 125.f * dt;
	_velocity = horiz * moveSpeed + vert * (moveSpeed * speedBoost);

	if (UserInput::keyboard.isPressed(GLFW_KEY_R)) {
		reset();
	}
}

glm::mat4 Camera::getViewMatrix() const {
	return glm::lookAt(_position, _position + _currentView, glm::vec3(0.f, 1.f, 0.f));
}

glm::mat4 Camera::getRotationMatrix() const {
	glm::quat pitchRotation = glm::angleAxis(glm::radians(_pitch), glm::vec3{ 1.f, 0.f, 0.f });
	glm::quat yawRotation = glm::angleAxis(glm::radians(_yaw), glm::vec3{ 0.f, 1.f, 0.f });

	glm::quat orientation = yawRotation * pitchRotation;
	return glm::toMat4(orientation);
}

void Camera::reset() {
	_position = SPAWNPOINT;
	_pitch = 0.f;
	_yaw = -90.f;
}
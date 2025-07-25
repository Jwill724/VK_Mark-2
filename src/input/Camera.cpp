#include "pch.h"

#include "Camera.h"
#include "profiler/EditorImgui.h"
#include "renderer/RenderScene.h"

void Camera::processInput(GLFWwindow* window, const float deltaTime) {
	UserInput::updateLocalInput(window, true, true);

	// TODO: movement is slow asf in space station model, due to model units being too large so
	// some scaling factor will need to be added for this particular model
	float baseSpeed = UserInput::keyboard.isPressed(GLFW_KEY_LEFT_SHIFT) ? 15.0f : 5.0f;
	float moveSpeed = baseSpeed * deltaTime;

	// Mouse rotation, imgui can be properly used with free cam
	if (!ImGui::GetIO().WantCaptureMouse && UserInput::mouse.leftPressed) {
		float sensitivity = 30.0f;
		_yaw -= UserInput::mouse.delta.x * sensitivity;
		_pitch += UserInput::mouse.delta.y * sensitivity;
		_pitch = std::clamp(_pitch, -89.0f, 89.0f);
	}

	float radPitch = glm::radians(_pitch);
	float radYaw = glm::radians(_yaw);

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
	if (UserInput::keyboard.isPressed(GLFW_KEY_W)) horiz += flatFoward;
	if (UserInput::keyboard.isPressed(GLFW_KEY_S)) horiz -= flatFoward;
	if (UserInput::keyboard.isPressed(GLFW_KEY_A)) horiz -= rightFoward;
	if (UserInput::keyboard.isPressed(GLFW_KEY_D)) horiz += rightFoward;

	glm::vec3 vert(0.0f);
	if (UserInput::keyboard.isPressed(GLFW_KEY_SPACE)) vert += up * upWorld;
	if (UserInput::keyboard.isPressed(GLFW_KEY_LEFT_CONTROL)) vert -= up * upWorld;


	if (glm::length(horiz) > 0.0f) horiz = glm::normalize(horiz);
	if (glm::length(vert) > 0.0f) vert = glm::normalize(vert);

	// scale speed on whole axis while frame independent
	_velocity = (horiz + vert) * moveSpeed;

	if (UserInput::keyboard.isPressed(GLFW_KEY_R)) {
		reset();
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
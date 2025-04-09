#include "pch.h"

#include "Camera.h"
#include "imgui/EditorImgui.h"

void Camera::update(GLFWwindow* window, float& lastTime) {
	float currentTime = static_cast<float>(glfwGetTime());
	float deltaTime = currentTime - lastTime;
	lastTime = currentTime;

	processInput(window, deltaTime);

	position += velocity;
}

void Camera::processInput(GLFWwindow* window, float dt) {
	UserInput::updateLocalInput(window);

	float baseSpeed = UserInput::keyboard.isPressed(GLFW_KEY_LEFT_SHIFT) ? 60.f : 30.f;
	float speed = baseSpeed * dt;

	// Mouse rotation, imgui can be properly used with free cam
	if (!ImGui::GetIO().WantCaptureMouse && UserInput::mouse.leftPressed) {
		float sensitivity = 30.f;
		yaw -= UserInput::mouse.delta.x* sensitivity;
		pitch += UserInput::mouse.delta.y * sensitivity;
		pitch = std::clamp(pitch, -89.f, 89.f);
	}

	float radPitch = glm::radians(pitch);
	float radYaw = glm::radians(yaw);

	glm::vec3 front = glm::normalize(glm::vec3(
		cos(radPitch) * cos(radYaw),
		sin(radPitch),
		cos(radPitch) * sin(radYaw)
	));

	glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.f, 1.f, 0.f)));
	glm::vec3 up = glm::normalize(glm::cross(right, front));

	velocity = glm::vec3(0.f);
	if (UserInput::keyboard.isPressed(GLFW_KEY_W)) velocity += front;
	if (UserInput::keyboard.isPressed(GLFW_KEY_S)) velocity -= front;
	if (UserInput::keyboard.isPressed(GLFW_KEY_A)) velocity -= right;
	if (UserInput::keyboard.isPressed(GLFW_KEY_D)) velocity += right;
	if (UserInput::keyboard.isPressed(GLFW_KEY_SPACE)) velocity += up;
	if (UserInput::keyboard.isPressed(GLFW_KEY_LEFT_CONTROL)) velocity -= up;

	if (glm::length(velocity) > 0.f)
		velocity = glm::normalize(velocity) * speed;

	if (UserInput::keyboard.isPressed(GLFW_KEY_R)) {
		reset();
	}
}

glm::mat4 Camera::getViewMatrix() {
	glm::vec3 front;
	front.x = cos(glm::radians(pitch)) * cos(glm::radians(yaw));
	front.y = sin(glm::radians(pitch));
	front.z = cos(glm::radians(pitch)) * sin(glm::radians(yaw));
	front = glm::normalize(front);

	return glm::lookAt(position, position + front, glm::vec3(0.f, 1.f, 0.f));
}

glm::mat4 Camera::getRotationMatrix() {
	glm::quat pitchRotation = glm::angleAxis(glm::radians(pitch), glm::vec3{ 1.f, 0.f, 0.f });
	glm::quat yawRotation = glm::angleAxis(glm::radians(yaw), glm::vec3{ 0.f, 1.f, 0.f });

	glm::quat orientation = yawRotation * pitchRotation;
	return glm::toMat4(orientation);
}

void Camera::reset() {
	position = glm::vec3(30.f, -00.f, -085.f);
	pitch = 0.f;
	yaw = -90.f;
}
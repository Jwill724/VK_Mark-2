#include "pch.h"

#include "UserInput.h"
#include "renderer/RenderScene.h"
#include "Engine.h"

namespace UserInput {
	MouseState mouse;
	KeyboardState keyboard;

	static glm::vec2 lastPos;
	static bool firstMouse = true;
	static bool hideCursor = false;

	void SetCursorPos(GLFWwindow* window, VkExtent2D windowExtent);
	void NormalizeMousePos(GLFWwindow* window, MouseState& mouse, VkExtent2D windowExtent);
}

void UserInput::SetCursorPos(GLFWwindow* window, VkExtent2D windowExtent) {
	glfwSetCursorPos(window, static_cast<double>(windowExtent.width / 2.0), static_cast<double>(windowExtent.height / 2.0));
}

void UserInput::NormalizeMousePos(GLFWwindow* window, MouseState& mouse, VkExtent2D windowExtent) {
	glfwGetCursorPos(window, &mouse.mousePos.x, &mouse.mousePos.y);
	float aspectRatio = static_cast<float>(windowExtent.width) / static_cast<float>(windowExtent.height);
	mouse.normalized.x = (2.f * static_cast<float>(mouse.mousePos.x) / static_cast<float>(windowExtent.width) - 1.f) * aspectRatio;
	mouse.normalized.y = 2.f * static_cast<float>(mouse.mousePos.y) / static_cast<float>(windowExtent.height) - 1.f;
}

void UserInput::MouseState::update(GLFWwindow* window) {
	double xpos, ypos;
	glfwGetCursorPos(window, &xpos, &ypos);

	position = glm::vec2(xpos, ypos);

	if (firstMouse) {
		lastPos = position;
		firstMouse = false;
	}

	delta = position - lastPos;
	lastPos = position;

	VkExtent2D windowExtent = Engine::getWindowExtent();
	double centerX = windowExtent.width / 2.0;
	double centerY = windowExtent.height / 2.0;

	leftPressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

	if (leftPressed) {
		if (!hideCursor) {
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
			SetCursorPos(window, windowExtent);
			hideCursor = true;
			glfwSetCursorPos(window, centerX, centerY);
			lastPos = glm::vec2(centerX, centerY);
		}
		else {
			glfwSetCursorPos(window, centerX, centerY);
			lastPos = glm::vec2(centerX, centerY); // prevent frame jump
		}
	}
	else {
		if (hideCursor) {
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
			hideCursor = false;
		}
	}

	rightPressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
}

void UserInput::KeyboardState::update(GLFWwindow* window) {
	keys[GLFW_KEY_W] = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
	keys[GLFW_KEY_A] = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
	keys[GLFW_KEY_S] = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
	keys[GLFW_KEY_D] = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;
	keys[GLFW_KEY_SPACE] = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
	keys[GLFW_KEY_LEFT_CONTROL] = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
	keys[GLFW_KEY_LEFT_SHIFT] = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
	keys[GLFW_KEY_R] = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
}

bool UserInput::KeyboardState::isPressed(int key) const {
	auto it = keys.find(key);
	return it != keys.end() && it->second;
}

void UserInput::updateLocalInput(GLFWwindow* window) {
	mouse.update(window);
	keyboard.update(window);
}

// TODO: Cursor doesn't align properly with middle of screen during resizing. But why?
void UserInput::resetMouse(GLFWwindow* window) {
	VkExtent2D windowExtent = Engine::getWindowExtent();
	double centerX = static_cast<double>(windowExtent.width) / 2.0;
	double centerY = static_cast<double>(windowExtent.height) / 2.0;

	glfwSetCursorPos(window, centerX, centerY);

	lastPos = glm::vec2(centerX, centerY);
	mouse.position = lastPos;
	firstMouse = true;
}
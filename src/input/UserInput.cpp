#include "pch.h"

#include "UserInput.h"
#include "renderer/RenderScene.h"
#include "Engine.h"

namespace UserInput {
	MouseState mouse;
	KeyboardState keyboard;

	static glm::vec2 lastPos;
	static bool firstMouse = false;

	void SetCursorPos(GLFWwindow* window, VkExtent2D windowExtent);

	// Maintains cursor to 1:1 with window sizing. Keeps mouse consistent and stable during a window resize.
	void NormalizeMousePos(GLFWwindow* window, VkExtent2D windowExtent);

	void handleMouseCapture(
		GLFWwindow* window,
		VkExtent2D* extent,
		bool& justClicked,
		glm::vec2& position,
		glm::vec2& delta
	);
}

void UserInput::SetCursorPos(GLFWwindow* window, VkExtent2D windowExtent) {
	glfwSetCursorPos(window, static_cast<double>(windowExtent.width / 2.0), static_cast<double>(windowExtent.height / 2.0));
}

void UserInput::NormalizeMousePos(GLFWwindow* window, VkExtent2D windowExtent) {
	glfwGetCursorPos(window, &mouse.mousePos.x, &mouse.mousePos.y);
	float aspectRatio = static_cast<float>(windowExtent.width) / static_cast<float>(windowExtent.height);
	mouse.normalized.x = (2.f * static_cast<float>(mouse.mousePos.x) / static_cast<float>(windowExtent.width) - 1.f) * aspectRatio;
	mouse.normalized.y = 2.f * static_cast<float>(mouse.mousePos.y) / static_cast<float>(windowExtent.height) - 1.f;
}

void UserInput::MouseState::update(GLFWwindow* window) {

	VkExtent2D* windowExtent = &Engine::getWindowExtent();

	NormalizeMousePos(window, *windowExtent);

	position = glm::vec2(mouse.normalized.x, mouse.normalized.y);

	if (firstMouse) {
		lastPos = position;
		firstMouse = false;
	}

	delta = position - lastPos;
	lastPos = position;

	leftPressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
	//rightPressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

	// --- Left click: free cam ---
	if (leftPressed && !ImGui::GetIO().WantCaptureMouse) {
		if (!leftHideCursor) {
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
			SetCursorPos(window, *windowExtent);
			leftHideCursor = true;
			leftJustClicked = true;
		}
		handleMouseCapture(window, windowExtent, leftJustClicked, position, delta);
	}
	else if (leftHideCursor) {
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		leftHideCursor = false;
	}

	//// --- Right click: unknown use ---
	//if (rightPressed && !ImGui::GetIO().WantCaptureMouse) {
	//	if (!rightHideCursor) {
	//		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
	//		SetCursorPos(window, *windowExtent);
	//		rightHideCursor = true;
	//		rightJustClicked = true;
	//	}
	//	handleMouseCapture(window, windowExtent, rightJustClicked, position, delta);
	//}
	//else if (rightHideCursor) {
	//	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	//	rightHideCursor = false;
	//}
}

void UserInput::KeyboardState::update(GLFWwindow* window) {
	// First check if window is closing
	keys[GLFW_KEY_ESCAPE] = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
	if (keys[GLFW_KEY_ESCAPE]) {
		glfwSetWindowShouldClose(window, true);
	}

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

void UserInput::updateLocalInput(GLFWwindow* window, bool mouseEnable, bool keyboardEnable) {
	if (mouseEnable) {
		mouse.update(window);
	}
	if (keyboardEnable) {
		keyboard.update(window);
	}
}

// shits scuffed but it works
void UserInput::handleMouseCapture(GLFWwindow* window, VkExtent2D* extent, bool& justClicked, glm::vec2& position, glm::vec2& delta) {
	SetCursorPos(window, *extent);  // always reset to center
	NormalizeMousePos(window, Engine::getWindowExtent());
	position = glm::vec2(mouse.normalized.x, mouse.normalized.y);

	if (justClicked) {
		lastPos = position;
		justClicked = false;  // allow delta on next frame
		delta = glm::vec2(0.f);  // prevent one-frame spike
	}
	else {
		delta = position - lastPos;
		lastPos = position;
	}
}
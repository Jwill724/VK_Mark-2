#include "pch.h"

#include "UserInput.h"
#include "engine/Engine.h"

// TODO:
// Add alt-tab capabilities
// Full screen sizing

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
	glfwSetCursorPos(window, static_cast<double>(windowExtent.width) / 2.0, static_cast<double>(windowExtent.height) / 2.0);
}

void UserInput::NormalizeMousePos(GLFWwindow* window, VkExtent2D windowExtent) {
	glfwGetCursorPos(window, &mouse.mousePos.x, &mouse.mousePos.y);
	float aspectRatio = static_cast<float>(windowExtent.width) / static_cast<float>(windowExtent.height);
	mouse.normalized.x = (2.0f * static_cast<float>(mouse.mousePos.x) / static_cast<float>(windowExtent.width) - 1.0f) * aspectRatio;
	mouse.normalized.y = 2.0f * static_cast<float>(mouse.mousePos.y) / static_cast<float>(windowExtent.height) - 1.0f;
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

// TODO: Investigate and possibly refactor input system to SDL
//
// - Input "ghosting" occurs after stalls (e.g., clicking window, resizing window).
// - When a stall happens, GLFW event queue may lose or delay key/button release events.
// - This causes any keys held, mouse buttons, etc., to appear "stuck" until another physical press/release.
// - Even after resetting local input state post-stall, GLFW still processes stale/missing input.
// Bug can be replicated by holding a key into a window stall then releasing.

void UserInput::KeyboardState::update(GLFWwindow* window) {
	if (isPressed(GLFW_KEY_ESCAPE))
		glfwSetWindowShouldClose(window, true);

	for (int key : trackedKeys) {
		int state = glfwGetKey(window, key);
		bool isDown = (state == GLFW_PRESS || state == GLFW_REPEAT);

		KeyState& prevState = keyStates[key];
		KeyState newState = KeyState::None;

		switch (prevState) {
		case KeyState::None:
			newState = isDown ? KeyState::Pressed : KeyState::None;
			break;
		case KeyState::Pressed:
		case KeyState::Held:
			newState = isDown ? KeyState::Held : KeyState::Released;
			break;
		case KeyState::Released:
			newState = isDown ? KeyState::Pressed : KeyState::None;
			break;
		}

		if (!isDown && (prevState == KeyState::Held || prevState == KeyState::Pressed)) {
			newState = KeyState::Released;
		}

		prevState = newState;
	}
}

void UserInput::updateLocalInput(GLFWwindow* window) {
	mouse.update(window);
	keyboard.update(window);
}

bool UserInput::KeyboardState::isPressed(int key) const {
	auto it = keyStates.find(key);
	return it != keyStates.end() && it->second == KeyState::Pressed;
}

bool UserInput::KeyboardState::isHeld(int key) const {
	auto it = keyStates.find(key);
	if (it == keyStates.end()) return false;

	KeyState state = it->second;
	return state == KeyState::Held || state == KeyState::Pressed;
}

bool UserInput::KeyboardState::isReleased(int key) const {
	auto it = keyStates.find(key);
	return it != keyStates.end() && it->second == KeyState::Released;
}

void UserInput::KeyboardState::resetKeyStates() {
	for (auto& [key, state] : keyStates) {
		state = KeyState::None;
	}
}

// Mouse recentering for consistent deltas, even across frames/resizes
void UserInput::handleMouseCapture(GLFWwindow* window, VkExtent2D* extent, bool& justClicked, glm::vec2& position, glm::vec2& delta) {
	SetCursorPos(window, *extent);  // always reset to center
	NormalizeMousePos(window, Engine::getWindowExtent());
	position = glm::vec2(mouse.normalized.x, mouse.normalized.y);

	if (justClicked) {
		lastPos = position;
		justClicked = false;  // allow delta on next frame
		delta = glm::vec2(0.0f);  // prevent one-frame spike
	}
	else {
		delta = position - lastPos;
		lastPos = position;
	}
}
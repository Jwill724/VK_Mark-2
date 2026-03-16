//#include "pch.h"
//
//#include "UserInput.h"
//#include "engine/Engine.h"
//
//namespace UserInput
//{
//	MouseState _mouse;
//	KeyboardState _keyboard;
//
//	static glm::vec2 lastPos = glm::vec2(0.0f);
//	static bool firstMouse = true;
//
//	static bool gUiEnabled = true;
//
//	void setUiEnabled(bool enabled) {
//		gUiEnabled = enabled;
//	}
//
//	bool isUiEnabled() {
//		return gUiEnabled;
//	}
//
//	static void setCursorHidden(bool hidden)
//	{
//		if (hidden) {
//			SDL_HideCursor();
//		}
//		else {
//			SDL_ShowCursor();
//		}
//	}
//
//	static void setMouseCaptured(SDL_Window* window, bool captured)
//	{
//		SDL_SetWindowRelativeMouseMode(window, captured);
//		SDL_SetWindowMouseGrab(window, captured);
//	}
//
//	void forceReleaseMouseCapture(SDL_Window* window) {
//		if (_mouse.leftHideCursor) {
//			_mouse.leftHideCursor = false;
//			_mouse.leftPressed = false;
//			_mouse.leftJustClicked = false;
//
//			firstMouse = true;
//			lastPos = glm::vec2(0.0f);
//			_mouse.delta = glm::vec2(0.0f);
//
//			setMouseCaptured(window, false);
//			setCursorHidden(false);
//		}
//	}
//
//	static void SetCursorPos(SDL_Window* window, VkExtent2D windowExtent);
//
//	// Maintains cursor to 1:1 with window sizing. Keeps mouse consistent and stable during a window resize.
//	static void NormalizeMousePos(SDL_Window* window, VkExtent2D windowExtent);
//}
//
//static void UserInput::SetCursorPos(SDL_Window* window, VkExtent2D windowExtent)
//{
//	SDL_WarpMouseInWindow(
//		window,
//		static_cast<float>(windowExtent.width) * 0.5f,
//		static_cast<float>(windowExtent.height) * 0.5f
//	);
//}
//
//static void UserInput::NormalizeMousePos(SDL_Window* window, VkExtent2D windowExtent)
//{
//	(void)window;
//
//	float mouseX = 0.0f;
//	float mouseY = 0.0f;
//
//	SDL_GetMouseState(&mouseX, &mouseY);
//
//	_mouse.mousePos.x = static_cast<double>(mouseX);
//	_mouse.mousePos.y = static_cast<double>(mouseY);
//
//	if (windowExtent.width == 0 || windowExtent.height == 0) {
//		_mouse.normalized.x = 0.0f;
//		_mouse.normalized.y = 0.0f;
//		return;
//	}
//
//	const float widthF = static_cast<float>(windowExtent.width);
//	const float heightF = static_cast<float>(windowExtent.height);
//
//	const float aspectRatio = widthF / heightF;
//
//	_mouse.normalized.x =
//		(2.0f * (mouseX / widthF) - 1.0f) * aspectRatio;
//
//	_mouse.normalized.y =
//		2.0f * (mouseY / heightF) - 1.0f;
//}
//
//void UserInput::beginFrame(SDL_Window* window)
//{
//	_keyboard.beginFrame();
//	_mouse.beginFrame();
//
//	VkExtent2D windowExtent = Engine::getWindowExtent();
//	if (windowExtent.width == 0 || windowExtent.height == 0) return;
//
//	if (!_mouse.leftHideCursor) {
//		NormalizeMousePos(window, windowExtent);
//		_mouse.position = glm::vec2(_mouse.normalized.x, _mouse.normalized.y);
//
//		if (firstMouse) {
//			lastPos = _mouse.position;
//			firstMouse = false;
//		}
//
//		_mouse.delta = _mouse.position - lastPos;
//		lastPos = _mouse.position;
//		return;
//	}
//
//    SetCursorPos(window, windowExtent);
//
//	_mouse.normalized.x = 0.0f;
//	_mouse.normalized.y = 0.0f;
//	_mouse.position = glm::vec2(0.0f, 0.0f);
//
//	if (firstMouse || _mouse.leftJustClicked) {
//		lastPos = _mouse.position;
//		_mouse.delta = glm::vec2(0.0f);
//
//		firstMouse = false;
//		_mouse.leftJustClicked = false;
//		return;
//	}
//}
//
//
//// ============================================================
//// KeyboardState (event-driven)
//// ============================================================
//
//void UserInput::KeyboardState::beginFrame()
//{
//	for (int scancodeIndex = 0; scancodeIndex < SDL_SCANCODE_COUNT; scancodeIndex++)
//	{
//		KeyState& state = keyStates[scancodeIndex];
//
//		if (state == KeyState::Pressed) {
//			state = KeyState::Held;
//		}
//		else if (state == KeyState::Released) {
//			state = KeyState::None;
//		}
//	}
//}
//
//void UserInput::MouseState::handleEvent(const SDL_Event& event, SDL_Window* window)
//{
//	const VkExtent2D windowExtent = Engine::getWindowExtent();
//	switch (event.type)
//	{
//	case SDL_EVENT_MOUSE_MOTION:
//	{
//		if (windowExtent.width == 0 || windowExtent.height == 0) break;
//
//		const float widthF = static_cast<float>(windowExtent.width);
//		const float heightF = static_cast<float>(windowExtent.height);
//		const float aspectRatio = widthF / heightF;
//
//		if (leftHideCursor)
//		{
//			const float relX = static_cast<float>(event.motion.xrel);
//			const float relY = static_cast<float>(event.motion.yrel);
//
//			glm::vec2 normalizedDelta{};
//			normalizedDelta.x = (2.0f * (relX / widthF)) * aspectRatio;
//			normalizedDelta.y = 2.0f * (relY / heightF);
//
//			// Accumulate for the frame (beginFrame() zeroes it).
//			delta += normalizedDelta;
//
//			// Position is meaningless during capture, keep it centered.
//			normalized.x = 0.0f;
//			normalized.y = 0.0f;
//			position = glm::vec2(0.0f);
//		}
//		else
//		{
//			mousePos.x = static_cast<double>(event.motion.x);
//			mousePos.y = static_cast<double>(event.motion.y);
//
//			normalized.x =
//				(2.0f * (static_cast<float>(event.motion.x) / widthF) - 1.0f) * aspectRatio;
//
//			normalized.y =
//				2.0f * (static_cast<float>(event.motion.y) / heightF) - 1.0f;
//
//			position = glm::vec2(normalized.x, normalized.y);
//		}
//
//	} break;
//
//
//	case SDL_EVENT_MOUSE_BUTTON_DOWN:
//	{
//		if (event.button.button == SDL_BUTTON_LEFT)
//		{
//			leftPressed = true;
//			leftJustClicked = true;
//
//			if (!UserInput::isUiEnabled() || !ImGui::GetIO().WantCaptureMouse)
//			{
//				if (!leftHideCursor)
//				{
//					leftHideCursor = true;
//					firstMouse = true;
//					delta = glm::vec2(0.0f);
//
//					setCursorHidden(true);
//					setMouseCaptured(window, true);
//				}
//			}
//		}
//	} break;
//
//	case SDL_EVENT_MOUSE_BUTTON_UP:
//	{
//		if (event.button.button == SDL_BUTTON_LEFT)
//		{
//			leftPressed = false;
//			leftJustClicked = false;
//
//			if (leftHideCursor)
//			{
//				leftHideCursor = false;
//				firstMouse = true;
//				lastPos = glm::vec2(0.0f);
//
//				delta = glm::vec2(0.0f);
//
//				setMouseCaptured(window, false);
//				setCursorHidden(false);
//			}
//		}
//	} break;
//
//	default:
//		break;
//	}
//}
//
//void UserInput::KeyboardState::handleEvent(const SDL_Event& event)
//{
//	if (event.type == SDL_EVENT_KEY_DOWN)
//	{
//		if (event.key.repeat) return;
//
//		const SDL_Scancode scancode = event.key.scancode;
//		KeyState& state = keyStates[scancode];
//
//		if (state == KeyState::None || state == KeyState::Released) {
//			state = KeyState::Pressed;
//		}
//		else {
//			state = KeyState::Held;
//		}
//	}
//	else if (event.type == SDL_EVENT_KEY_UP)
//	{
//		const SDL_Scancode scancode = event.key.scancode;
//		keyStates[scancode] = KeyState::Released;
//	}
//}
//
//bool UserInput::KeyboardState::isPressed(SDL_Scancode sc) const
//{
//	return keyStates[sc] == KeyState::Pressed;
//}
//
//bool UserInput::KeyboardState::isHeld(SDL_Scancode sc) const
//{
//	const KeyState state = keyStates[sc];
//	return (state == KeyState::Held || state == KeyState::Pressed);
//}
//
//bool UserInput::KeyboardState::isReleased(SDL_Scancode sc) const
//{
//	return keyStates[sc] == KeyState::Released;
//}
//
//void UserInput::KeyboardState::resetKeyStates() {
//	for (auto& key : keyStates) {
//		key = KeyState::None;
//	}
//}
//
//void UserInput::handleSDLEvent(const SDL_Event& event, SDL_Window* window)
//{
//	_mouse.handleEvent(event, window);
//	_keyboard.handleEvent(event);
//}

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

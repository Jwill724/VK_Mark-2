#pragma once

#include "common/Vk_Types.h"

// User input provides functionality for
// any kind of input controller setup
namespace UserInput {
	enum class InputType {
		Keyboard,
		Mouse
	};

	struct MouseState {
		glm::vec2 position;
		glm::vec2 delta;
		float scrollOffset;
		bool leftPressed;
		bool rightPressed;

		struct NormalizedPos {
			float x, y;
		} normalized;

		struct MousePos {
			double x, y;
		} mousePos;

		MouseState() :
			position(0.f), delta(0.f), scrollOffset(0.f),
			leftPressed(false), rightPressed(false),
			normalized{ 0.f, 0.f }, mousePos{ 0.0, 0.0 } {}

		void update(GLFWwindow* window);
	};

	struct KeyboardState {
		std::unordered_map<int, bool> keys;

		void update(GLFWwindow* window);
		bool isPressed(int key) const;
	};

	extern MouseState mouse;
	extern KeyboardState keyboard;

	// in current scope of defined instance
	void updateLocalInput(GLFWwindow* window);

	// updates cursor position relative to window size
	void resetMouse(GLFWwindow* window);
}
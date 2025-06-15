#pragma once

#include "common/Vk_Types.h"

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
		bool leftHideCursor;
		bool leftJustClicked;

		bool rightPressed;
		bool rightHideCursor;
		bool rightJustClicked;

		// Used for setting up a [1, -1] for virtual mouse position
		// Mouse will spin out if window extent isn't 1:1
		struct NormalizedPos {
			float x, y;
		} normalized;

		struct MousePos {
			double x, y;
		} mousePos;

		MouseState() :
			position(0.f), delta(0.f), scrollOffset(0.f),
			leftPressed(false), rightPressed(false),
			leftHideCursor(false), rightHideCursor(false),
			leftJustClicked(false), rightJustClicked(false),
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
	void updateLocalInput(GLFWwindow* window, bool mouseEnable, bool keyboardEnable);
}
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
			position(0.0f), delta(0.0f), scrollOffset(0.0f),
			leftPressed(false), rightPressed(false),
			leftHideCursor(false), rightHideCursor(false),
			leftJustClicked(false), rightJustClicked(false),
			normalized{ 0.0f, 0.0f }, mousePos{ 0.0, 0.0 } {
		}

		void update(GLFWwindow* window);
	};

	enum struct KeyState {
		None,
		Pressed,
		Held,
		Released
	};

	struct KeyboardState {
		std::unordered_map<int, KeyState> keyStates;

		inline static constexpr std::array trackedKeys {
			GLFW_KEY_W, GLFW_KEY_A, GLFW_KEY_S, GLFW_KEY_D,
			GLFW_KEY_SPACE, GLFW_KEY_LEFT_CONTROL, GLFW_KEY_LEFT_SHIFT,
			GLFW_KEY_ESCAPE, GLFW_KEY_TAB, GLFW_KEY_P, GLFW_KEY_R
		};

		void update(GLFWwindow* window);
		bool isPressed(int key) const;
		bool isHeld(int key) const;
		bool isReleased(int key) const;

		void resetKeyStates();
	};

	extern MouseState mouse;
	extern KeyboardState keyboard;

	void updateLocalInput(GLFWwindow* window);
}
#pragma once

#include <unordered_map>
#include "Core.h"

struct GLFWwindow;

namespace UserInput
{
	enum class InputType
	{
		Keyboard,
		Mouse
	};

	struct MouseState
	{
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
		// Mouse will spin out if window m_extent isn't 1:1
		float normalizedPos[2];

		double mousePos[2];

		MouseState() :
			position(0.0f), delta(0.0f), scrollOffset(0.0f),
			leftPressed(false), rightPressed(false),
			leftHideCursor(false), rightHideCursor(false),
			leftJustClicked(false), rightJustClicked(false),
			normalizedPos{ 0.0f, 0.0f }, mousePos{ 0.0, 0.0 } {
		}

		void Update(GLFWwindow* window);
	};

	enum struct KeyState
	{
		None,
		Pressed,
		Held,
		Released
	};

	enum class Keys
	{
		W, A, S, D, SPACE,
		LEFT_CTRL, LEFT_SHIFT, ESC,
		TAB, R, F, P
	};

	constexpr std::array trackedKeys
	{
		Keys::W, Keys::A, Keys::S, Keys::D, Keys::SPACE,
		Keys::LEFT_CTRL, Keys::LEFT_SHIFT, Keys::ESC,
		Keys::TAB, Keys::R, Keys::F, Keys::P
	};

	struct KeyboardState
	{
		std::unordered_map<int, KeyState> keyStates;

		static int ToGlfw(Keys key);

		void update(GLFWwindow* window);
		bool isPressed(Keys key) const;
		bool isHeld(Keys key) const;
		bool isReleased(Keys key) const;

		void resetKeyStates();
	};

	extern MouseState mouse;
	extern KeyboardState keyboard;

	void updateLocalInput(GLFWwindow* window);

	void UpdateCachedWindowExtent(uint32_t w, uint32_t h);
	void NotifyWindowResized();
}

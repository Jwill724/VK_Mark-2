#pragma once

#include <array>
#include <cstdint>

struct GLFWwindow;

class Window final
{
public:
	Window() = default;
	Window(uint32_t width, uint32_t height, const char* name)
		: m_extentWidth(width)
		, m_extentHeight(height)
		, m_windowName(name)
	{
		InitWindow();
	}

	~Window() { Cleanup(); }

	void UpdateDynamicWindowSize() const;

	bool ThrottleIfWindowUnfocused(double sleepMs) const;

	void PollEvents() const;

	bool IsOpen() const;

	GLFWwindow* GetWindowHandle() const { return m_windowHandle; }

	void SetExtent(uint32_t width, uint32_t height) const noexcept
	{
		m_extentWidth = width;
		m_extentHeight = height;
	};

	std::array<uint32_t, 2> GetExtent() const;
private:
	void InitWindow();
	void Cleanup() const;

	GLFWwindow* m_windowHandle = nullptr;
	const char* m_windowName = nullptr;

	mutable uint32_t m_extentWidth = 0u;
	mutable uint32_t m_extentHeight = 0u;
};

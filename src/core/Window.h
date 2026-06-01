#pragma once

#include <array>
#include <cstdint>
#include <string>

struct GLFWwindow;
struct Extents2D;

class Window final
{
public:
	void Init(uint32_t width, uint32_t height, std::string name);
	void Cleanup() const;

	void UpdateWindowSize() const;

	bool ThrottleIfWindowUnfocused(double sleepMs) const;

	void PollEvents() const;

	bool IsOpen() const;

	GLFWwindow* GetWindowHandle() const { return m_windowHandle; }

	void SetExtent(uint32_t width, uint32_t height) const noexcept
	{
		m_extentWidth = width;
		m_extentHeight = height;
	};

	const Extents2D& GetExtent() const;
private:

	GLFWwindow* m_windowHandle = nullptr;
	std::string m_windowName;

	mutable uint32_t m_extentWidth = 0u;
	mutable uint32_t m_extentHeight = 0u;
};

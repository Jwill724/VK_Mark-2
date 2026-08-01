#include "pch.h"

#include "Window.h"
#include "EngineTypes.h"

static void framebufferResizeCallback(GLFWwindow* window, int width, int height)
{
	(void)width;
	(void)height;

	if (auto* win = static_cast<Window*>(glfwGetWindowUserPointer(window)))
		win->FlagResized();
}

bool Window::IsMinimized() const
{
	int width = 0, height = 0;
	glfwGetFramebufferSize(m_windowHandle, &width, &height);
	return width == 0 || height == 0;
}

bool Window::IsOpen() const
{
	return !(glfwWindowShouldClose(m_windowHandle));
}

bool Window::ThrottleIfWindowUnfocused(double sleepMs) const
{
	if (!glfwGetWindowAttrib(m_windowHandle, GLFW_VISIBLE) || !glfwGetWindowAttrib(m_windowHandle, GLFW_FOCUSED))
	{
		glfwWaitEventsTimeout(sleepMs);
		return true;
	}
	return false;
}

void Window::Init(uint32_t width, uint32_t height, std::string name)
{
	m_extentWidth = width;
	m_extentHeight = height;
	m_windowName = name;

	int glfwResult = glfwInit();
	if (!glfwResult) { ASSERT(glfwResult && "Failed to initialize GLFW!"); }

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

	m_windowHandle = glfwCreateWindow(m_extentWidth, m_extentHeight, m_windowName.data(), nullptr, nullptr);
	if (!m_windowHandle) { ASSERT(m_windowHandle && "Failed to initialize GLFW window!"); }

	GLFWmonitor* mon = glfwGetPrimaryMonitor();
	const GLFWvidmode* vm = glfwGetVideoMode(mon);
	int mx = 0, my = 0;
	glfwGetMonitorPos(mon, &mx, &my);
	int x = mx + (vm->width - static_cast<int>(m_extentWidth)) / 2;
	int y = my + (vm->height - static_cast<int>(m_extentHeight)) / 2;
	glfwSetWindowPos(m_windowHandle, x, y);

	glfwSetWindowUserPointer(m_windowHandle, this);
	glfwSetFramebufferSizeCallback(m_windowHandle, framebufferResizeCallback);
}

Extents2D Window::GetExtent() const
{
	int w = 0, h = 0;
	glfwGetFramebufferSize(m_windowHandle, &w, &h);
	m_extentWidth  = static_cast<uint32_t>(w);
	m_extentHeight = static_cast<uint32_t>(h);
	return { m_extentWidth, m_extentHeight };
}

void Window::PollEvents() const
{
	glfwPollEvents();
}

void Window::Cleanup() const
{
	glfwDestroyWindow(m_windowHandle);
	glfwTerminate();
}

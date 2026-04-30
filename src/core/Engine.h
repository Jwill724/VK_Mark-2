#pragma once

struct GLFWwindow;
class Window;

namespace Engine
{
	GLFWwindow* GetWindow();
	const Window& WindowModMode();

	void InitWindow();
	void ResetWindow();

	bool IsInitialized();

	// inits everything, controls runtime, and cleans up
	void Run();
}

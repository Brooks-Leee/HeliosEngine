#include "core/Platform/Window.h"
#include "core/Render/Vulkan/VulkanRenderer.h"

#include <exception>
#include <iostream>
#include <windows.h>

int WINAPI WinMain(HINSTANCE HInstance, HINSTANCE HPrevInstance, LPSTR LpCmdLine, int NCmdShow)
{
	(void) HPrevInstance;
	(void) LpCmdLine;
	(void) NCmdShow;

	// Phase 1.1: Win32 窗口
	Helios::Window Window(L"HeliosEngine — Vulkan Hello Triangle", 1280, 720);
	std::cout << "Window created. HWND: " << Window.GetHwnd() << "\n";

	// Phase 1.2: Vulkan 渲染器
	Helios::VulkanRenderer Renderer;

	try
	{
		Renderer.Initialize(Window.GetHwnd(), Window.GetWidth(), Window.GetHeight());

		// 游戏循环
		while (Window.ProcessMessages())
		{
			Renderer.Render();
		}
	}
	catch (const std::exception& E)
	{
		std::cerr << "[FATAL] " << E.what() << "\n";
		MessageBoxA(nullptr, E.what(), "Vulkan Error", MB_ICONERROR | MB_OK);
		return 1;
	}

	// 先关闭 renderer，再销毁窗口
	Renderer.Shutdown();

	std::cout << "Clean exit.\n";
	return 0;
}

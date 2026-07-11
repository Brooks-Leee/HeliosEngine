#pragma once

#include <windows.h>

namespace Helios
{

// =========================================================================
// Window — Win32 窗口的 RAII 封装
//
// 构造时创建 HWND + 显示窗口，析构时自动销毁。不需要手动管理生命周期。
//
// 设计要点：
//
// 1. 为什么用 PeekMessage 而不是 GetMessage？
//    GetMessage 队列空时阻塞线程 → 渲染循环停摆 → GPU 空转。
//    PeekMessage 队列空时立即返回 → 游戏循环持续运行。
//    这是"事件驱动应用"和"游戏引擎"消息泵的本质区别。
//
// 2. 为什么不用 GLFW/SDL？
//    学习目标之一就是理解 Windows 消息机制。Win32 是 Windows 游戏引擎
//    的底层基础，GLFW 只是把 CreateWindow + PeekMessage 包了一层。
//
// 3. RAII：构造 = CreateWindowEx + ShowWindow，析构 = DestroyWindow。
//    禁止拷贝——HWND 生命周期由本类独占。
// =========================================================================
class Window
{
  public:
	// 构造即创建窗口。Title 是标题栏文字，InWidth/InHeight 是客户区尺寸。
	Window(const wchar_t* Title, int InWidth, int InHeight);
	~Window();

	// 原生窗口句柄。DX12/Vulkan 创建 SwapChain 时需要传入。
	HWND GetHwnd() const
	{
		return m_Hwnd;
	}

	// 泵送一帧的 Windows 消息。收到 WM_QUIT 时返回 false → 游戏循环退出。
	bool ProcessMessages();

	// 客户区尺寸（不含标题栏和边框）。渲染器用这个尺寸创建 SwapChain。
	int GetWidth() const
	{
		return m_Width;
	}
	int GetHeight() const
	{
		return m_Height;
	}

	// 禁止拷贝。HWND 生命周期由本类独占，不共享。
	Window(const Window&) = delete;
	Window& operator=(const Window&) = delete;

  private:
	// 窗口过程。Windows 把消息发给这个函数。
	// Hwnd=触发消息的窗口句柄, Msg=消息类型(WM_DESTROY 等), WParam/LParam=消息参数
	static LRESULT CALLBACK WindowProc(HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam);

	HWND m_Hwnd = nullptr; // 窗口句柄—Windows 内核用来追踪窗口的整数 ID
	int m_Width = 0;	   // 客户区宽度（像素）
	int m_Height = 0;	   // 客户区高度（像素）
};

} // namespace Helios

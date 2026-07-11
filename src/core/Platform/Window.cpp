#include "Window.h"

namespace Helios
{

namespace
{
// 窗口类的唯一标识名。仅此翻译单元内部使用。
constexpr const wchar_t* KWindowClassName = L"HeliosEngineWindow";
} // namespace

Window::Window(const wchar_t* Title, int InWidth, int InHeight)
{
	HINSTANCE Instance = GetModuleHandle(nullptr);

	// 1. 注册窗口类——告诉 Windows 用哪个窗口过程处理消息。
	WNDCLASSEXW Wc = {};
	Wc.cbSize = sizeof(WNDCLASSEXW);
	Wc.style = CS_HREDRAW | CS_VREDRAW;
	Wc.lpfnWndProc = WindowProc;
	Wc.hInstance = Instance;
	Wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	Wc.lpszClassName = KWindowClassName;

	RegisterClassExW(&Wc);

	// 2. 根据目标客户区尺寸反算窗口外框尺寸。
	//    CreateWindow 按窗口外框定位；AdjustWindowRect 从你想要的
	//    客户区尺寸反推出需要的外框尺寸。
	RECT WindowRect = {0, 0, InWidth, InHeight};
	AdjustWindowRect(&WindowRect, WS_OVERLAPPEDWINDOW, FALSE);
	int AdjustedWidth = WindowRect.right - WindowRect.left;
	int AdjustedHeight = WindowRect.bottom - WindowRect.top;

	// 3. 创建窗口。
	m_Hwnd = CreateWindowExW(0, KWindowClassName, Title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
							 AdjustedWidth, AdjustedHeight, nullptr, nullptr, Instance,
							 this // 通过 lpParam 把 this 传给 WindowProc
	);

	// 4. 显示窗口——CreateWindowEx 只管创建不管显示。
	ShowWindow(m_Hwnd, SW_SHOW);

	m_Width = InWidth;
	m_Height = InHeight;
}

Window::~Window()
{
	if (m_Hwnd)
	{
		DestroyWindow(m_Hwnd);
		m_Hwnd = nullptr;
	}
	UnregisterClassW(KWindowClassName, GetModuleHandle(nullptr));
}

bool Window::ProcessMessages()
{
	MSG Msg = {};

	// PeekMessage——游戏循环的最佳搭档。
	// PM_REMOVE：处理完就从队列移除。
	// 0, 0：不做消息过滤，所有消息都处理。
	while (PeekMessageW(&Msg, nullptr, 0, 0, PM_REMOVE))
	{
		if (Msg.message == WM_QUIT)
		{
			return false; // 窗口正在关闭——通知游戏循环退出。
		}
		TranslateMessage(&Msg);
		DispatchMessageW(&Msg);
	}
	return true; // 窗口仍存活——继续游戏循环。
}

LRESULT CALLBACK Window::WindowProc(HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam)
{
	switch (Msg)
	{
	case WM_DESTROY:
		// 用户点了 X 按钮。投递 WM_QUIT 让 ProcessMessages 返回 false，
		// 游戏循环就会干净地退出。
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProcW(Hwnd, Msg, WParam, LParam);
}

} // namespace Helios

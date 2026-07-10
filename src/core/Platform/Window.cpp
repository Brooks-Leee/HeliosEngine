#include "Window.h"

namespace Helios {

namespace {
    // 窗口类的唯一标识名。仅此翻译单元内部使用。
    constexpr const wchar_t* kWindowClassName = L"HeliosEngineWindow";
}

Window::Window(const wchar_t* title, int width, int height) {
    HINSTANCE hInstance = GetModuleHandle(nullptr);

    // 1. 注册窗口类——告诉 Windows 用哪个窗口过程处理消息。
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClassName;

    RegisterClassExW(&wc);

    // 2. 根据目标客户区尺寸反算窗口外框尺寸。
    //    CreateWindow 按窗口外框定位；AdjustWindowRect 从你想要的
    //    客户区尺寸反推出需要的外框尺寸。
    RECT windowRect = { 0, 0, width, height };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);
    int adjustedWidth = windowRect.right - windowRect.left;
    int adjustedHeight = windowRect.bottom - windowRect.top;

    // 3. 创建窗口。
    m_hwnd = CreateWindowExW(
        0,
        kWindowClassName,
        title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        adjustedWidth, adjustedHeight,
        nullptr,
        nullptr,
        hInstance,
        this  // 通过 lpParam 把 this 传给 WindowProc
    );

    // 4. 显示窗口——CreateWindowEx 只管创建不管显示。
    ShowWindow(m_hwnd, SW_SHOW);

    m_width = width;
    m_height = height;
}

Window::~Window() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    UnregisterClassW(kWindowClassName, GetModuleHandle(nullptr));
}

bool Window::ProcessMessages() {
    MSG msg = {};

    // PeekMessage——游戏循环的最佳搭档。
    // PM_REMOVE：处理完就从队列移除。
    // 0, 0：不做消息过滤，所有消息都处理。
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return false;  // 窗口正在关闭——通知游戏循环退出。
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;  // 窗口仍存活——继续游戏循环。
}

LRESULT CALLBACK Window::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY:
        // 用户点了 X 按钮。投递 WM_QUIT 让 ProcessMessages 返回 false，
        // 游戏循环就会干净地退出。
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace Helios

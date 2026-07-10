#pragma once

#include <windows.h>

namespace Helios {

// Win32 窗口的轻量 RAII 封装。
// 构造时创建 HWND，析构时销毁——无需手动管理生命周期。
//
// 为什么用 PeekMessage 而不是 GetMessage：
//   GetMessage 在消息队列为空时阻塞调用线程。这意味着渲染循环停摆、
//   GPU 空转，你只是在等 Windows 投递下一条消息。PeekMessage 在队列
//   为空时立即返回，游戏循环可以持续运行。这是"事件驱动应用的消息泵"
//   和"游戏引擎的游戏循环"之间的本质区别。
class Window {
public:
    Window(const wchar_t* title, int width, int height);
    ~Window();

    // 把原生句柄暴露给 DX12/Vulkan，SwapChain 创建时需要。
    HWND GetHwnd() const { return m_hwnd; }

    // 泵送一帧的消息。窗口关闭（收到 WM_QUIT）时返回 false。
    bool ProcessMessages();

    // 客户区尺寸——渲染器实际绘制到的区域。
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

    // 禁止拷贝。HWND 生命周期由本类独占，不共享。
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    int m_width = 0;
    int m_height = 0;
};

} // namespace Helios

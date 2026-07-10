#pragma once

#include <windows.h>

namespace Helios {

// Lightweight RAII wrapper around a Win32 window.
// Owns the HWND from construction to destruction — no manual lifecycle.
//
// Why PeekMessage instead of GetMessage:
//   GetMessage blocks the calling thread when the message queue is empty.
//   That means your render loop stops, GPU goes idle, and you burn vsync
//   waiting for Windows to deliver the next message. PeekMessage returns
//   immediately when the queue is empty, so the game loop keeps running.
//   This is the fundamental difference between an app with a message pump
//   and a game engine with a game loop.
class Window {
public:
    Window(const wchar_t* title, int width, int height);
    ~Window();

    // Give DX12/Vulkan the native handle they need for swapchain creation.
    HWND GetHwnd() const { return m_hwnd; }

    // Pump one frame worth of messages. Returns false when the user has
    // closed the window (WM_QUIT received).
    bool ProcessMessages();

    // Client area dimensions — what the renderer actually draws into.
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

    // Non-copyable. HWND lifetime is owned, not shared.
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    int m_width = 0;
    int m_height = 0;
};

} // namespace Helios

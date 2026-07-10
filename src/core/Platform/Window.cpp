#include "Window.h"

namespace Helios {

namespace {
    // Unique name for the window class. No one outside this TU needs it.
    constexpr const wchar_t* kWindowClassName = L"HeliosEngineWindow";
}

Window::Window(const wchar_t* title, int width, int height) {
    HINSTANCE hInstance = GetModuleHandle(nullptr);

    // 1. Register the window class — tells Windows about our WindowProc.
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClassName;

    RegisterClassExW(&wc);

    // 2. Calculate the window rect that gives us the requested client area.
    //    CreateWindow positions the *outer* window; AdjustWindowRect back-
    //    computes the outer size from the desired inner (client) size.
    RECT windowRect = { 0, 0, width, height };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);
    int adjustedWidth = windowRect.right - windowRect.left;
    int adjustedHeight = windowRect.bottom - windowRect.top;

    // 3. Create the window.
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
        this  // Pass the Window* to WindowProc via lpParam
    );

    // 4. Show the window — CreateWindowEx only creates it, doesn't display.
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

    // PeekMessage — the game loop's best friend.
    // PM_REMOVE: take the message off the queue.
    // 0, 0: no filtering — we want every message.
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return false;  // Window closing — tell the game loop to exit.
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;  // Still alive — keep the game loop running.
}

LRESULT CALLBACK Window::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY:
        // The user clicked the X button. Post WM_QUIT so ProcessMessages
        // returns false and the game loop exits cleanly.
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace Helios

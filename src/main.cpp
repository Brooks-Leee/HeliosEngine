#include "core/Platform/Window.h"

#include <windows.h>
#include <iostream>

int WINAPI WinMain(HINSTANCE hInstance,
                   HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine,
                   int nCmdShow)
{
    // Phase 1.1: Create a window and run the message loop.
    // Phase 1.2 will add DX12 initialization here.
    // Phase 1.3 will add Vulkan initialization here.

    Helios::Window window(L"HeliosEngine — Dawn is coming.", 1280, 720);

    std::cout << "Window created. HWND: " << window.GetHwnd() << "\n";

    // Game loop placeholder. ProcessMessages returns false when the
    // window closes (WM_QUIT), so the loop exits cleanly.
    while (window.ProcessMessages()) {
        // Frame will go here.
    }

    std::cout << "Window closed. Goodbye.\n";
    return 0;
}

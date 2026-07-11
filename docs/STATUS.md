# Current Status

> **活指针**—每次会话结束更新。新 agent 读此文件就知道从哪继续。

## 位置

- **Phase**: 1.2 Vulkan Hello Triangle — 三角形绘制跑通 ✅
- **上次完成**: Vulkan 全流程（Instance→Device→SwapChain→Pipeline→Draw），UE 编码规范落地，w+颜色实验
- **下次**: 继续深挖 Vulkan（Vertex Buffer / Fence 多帧并行）或开始 1.3 DX12

## 入口文件

| 文件 | 用途 |
|------|------|
| `src/main.cpp` | 程序入口 |
| `src/core/Render/Vulkan/VulkanRenderer.h` | Vulkan 渲染器类声明 |
| `src/core/Render/Vulkan/VulkanRenderer.cpp` | Vulkan 渲染器全实现 |
| `src/core/Platform/Window.h/.cpp` | Win32 RAII 窗口 |
| `docs/roadmap.md` | 全部 Phase 进度 |
| `docs/notes/` | 学习笔记（6 篇） |

## 最近一次会话

见 `docs/sessions/2026-07-12.md`

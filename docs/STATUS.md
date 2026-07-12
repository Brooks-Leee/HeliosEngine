# Current Status

> **活指针**—每次会话结束由 agent 更新。

## 位置

- **Phase**: 1.2 Vulkan Hello Triangle ✅ 完成
- **上次完成**: 多物件渲染落地——RenderPass/Framebuffer 不动，RecordCommandBuffer 内循环 draw() 5 次，用 push constant 传每个物件的 offset/scale；shader 改为从 .spv 文件加载（CMake 注入 HELIOS_SHADER_DIR，去掉硬编码 SPIR-V 数组）；全部注释改为英文（开发者要求，避免中文注释变成"偷懒速读"拐杖）。subpass 演示按用户要求 `git stash`（stash@{0}: subpass-inputattachment-demo）暂存，暂不提交。
- **下次**: Vertex Buffer + MVP 矩阵（真正的 mesh 变换写法）；多 pass（offscreen→post）是后续引入 subpass / 多 render pass 的自然场景。

## 入口文件

| 文件 | 用途 |
|------|------|
| `src/main.cpp` | 程序入口 |
| `src/core/Render/Vulkan/VulkanRenderer.h` | Vulkan 渲染器类声明（每个成员都有注释解释用途） |
| `src/core/Render/Vulkan/VulkanRenderer.cpp` | Vulkan 渲染器全实现（英文注释解释"为什么"） |
| `src/core/Platform/Window.h/.cpp` | Win32 RAII 窗口 |
| `docs/roadmap.md` | 全部 Phase 进度 |
| `docs/notes/` | 学习笔记（6 篇） |
| `docs/sessions/` | 每次会话的 checkpoint |

## 编码规范速查

- `m_` 前缀 + PascalCase 成员、Allman 花括号、禁用 `auto`（除 `reinterpret_cast`）、不缩写
- `.clang-format` 已配好，改代码后跑 `clang-format -i 文件.cpp`
- 注释用**英文**，只解释 WHY / 这一段干嘛，不逐行复述代码

## 最近一次会话

见 `docs/sessions/2026-07-13.md`

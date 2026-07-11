# Current Status

> **活指针**—每次会话结束由 agent 更新。

## 位置

- **Phase**: 1.2 Vulkan Hello Triangle ✅ 完成
- **上次完成**: 三角形绘制跑通，UE 编码规范落地，所有代码注释重写为自解释风格，项目结构建立（STATUS + sessions + clang-format + post-commit hook）
- **下次**: 继续深挖 Vulkan（Vertex Buffer → Uniform Buffer → Texture，roadmap Phase 2）或开始 1.3 DX12 做对比

## 入口文件

| 文件 | 用途 |
|------|------|
| `src/main.cpp` | 程序入口 |
| `src/core/Render/Vulkan/VulkanRenderer.h` | Vulkan 渲染器类声明（每个成员都有注释解释用途） |
| `src/core/Render/Vulkan/VulkanRenderer.cpp` | Vulkan 渲染器全实现（每步注释解释"为什么"） |
| `src/core/Platform/Window.h/.cpp` | Win32 RAII 窗口 |
| `docs/roadmap.md` | 全部 Phase 进度 |
| `docs/notes/` | 学习笔记（6 篇） |
| `docs/sessions/` | 每次会话的 checkpoint |

## 编码规范速查

- `m_` 前缀 + PascalCase 成员、Allman 花括号、禁用 `auto`（除 `reinterpret_cast`）、不缩写
- `.clang-format` 已配好，改代码后跑 `clang-format -i 文件.cpp`

## 最近一次会话

见 `docs/sessions/2026-07-12.md`

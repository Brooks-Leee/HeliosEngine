# Current Status

> **活指针**—每次会话结束由 agent 更新。

## 位置

- **Phase**: 1.2 Vulkan Hello Triangle ✅ 完成；1.2.x 扩展（Vertex Buffer ✅、MVP ✅）完成。**2026-08-01 起项目方向转向（见下）。**
- **项目方向（2026-08-01 战略决策）**: 与 **Godot strand-fur 桌面宠物**项目（写实可交互毛发，重度依赖 compute shader，跨平台 PC + 移动）**并行推进**——HeliosEngine 持续做引擎学习；宠物项目里遇到的 compute/barrier/DX12 底层问题会回到这里研究。决策详情见 `docs/sessions/2026-08-01.md`。
- **HeliosEngine 上次完成**: vertex buffer 升级 device-local + staging（`e67bcbe`）；MVP 矩阵 dynamic-offset UBO + DirectXMath 3D 相机（`c75a202`）；笔记 1.9 重写（GPU 读数据路径，`d4e9dcc`）。笔记 1.7–1.10 齐。
- **HeliosEngine 下次**: depth attachment；Phase 1.3 DX12 Hello Triangle。
- **宠物项目路径**: M0 Godot RenderingDevice compute spike → M1 shell fur → M2 strand 质量分级（PC 高密度/移动低密度）→ M3 交互（抚摸/吹风 → 引导发丝 XPBD）→ M4 产品工程（透明窗/常驻低功耗/打包）。模型动画走采购（Sketchfab + Mixamo）。

## 入口文件

| 文件 | 用途 |
|------|------|
| `src/main.cpp` | 程序入口 |
| `src/core/Render/Vulkan/VulkanRenderer.h` | Vulkan 渲染器类声明（每个成员都有注释解释用途） |
| `src/core/Render/Vulkan/VulkanRenderer.cpp` | Vulkan 渲染器全实现（英文注释解释"为什么"） |
| `src/core/Platform/Window.h/.cpp` | Win32 RAII 窗口 |
| `docs/roadmap.md` | 全部 Phase 进度 |
| `docs/notes/` | 学习笔记（13 篇） |
| `docs/sessions/` | 每次会话的 checkpoint |

## 编码规范速查

- `m_` 前缀 + PascalCase 成员、Allman 花括号、禁用 `auto`（除 `reinterpret_cast`）、不缩写
- `.clang-format` 已配好，改代码后跑 `clang-format -i 文件.cpp`
- 注释用**英文**，只解释 WHY / 这一段干嘛，不逐行复述代码

## 最近一次会话

见 `docs/sessions/2026-08-01.md`

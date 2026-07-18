# Project Thinking Guides

这些指南补充 `native` 与 `shaders` 层规范，帮助修改者在动手前检查本项目最容易遗漏的边界。

| Guide | Use when |
|---|---|
| [Cross-Layer Thinking Guide](./cross-layer-thinking-guide.md) | 修改输入、建模、渲染、HLSL、透明呈现或尺寸生命周期中的任意跨层数据流 |
| [Code Reuse Thinking Guide](./code-reuse-thinking-guide.md) | 修改常量、数据布局、脏矩形算法、回退链或准备新增帮助函数 |

## Quick Triggers

出现以下任一情况时，先阅读对应指南：

- 修改 `InkPoint`、`HighlighterPrimitive`、常量缓冲区或 HLSL 寄存器。
- 修改 L0/L1/L2 的提交、合成、清理或 resize 语义。
- 修改窗口过程与主绘制线程之间的数据传递。
- 修改透明呈现模式、交换链参数或回退顺序。
- 同一个阈值或语义似乎出现在 C++、HLSL、工程文件或项目说明中的多个位置。
- 准备处理 `.vcxproj` 中 `main2.cpp`、`main3.cpp`、`renderer2.h`、`shader.hlsl` 的残留引用，或复用 `ResTest/` 中的参考代码。

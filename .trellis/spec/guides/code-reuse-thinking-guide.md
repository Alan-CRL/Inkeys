# Code Reuse Thinking Guide

本项目的复用重点是保持行为契约只有一个权威位置，而不是抽取尽可能多的帮助函数。

## Search Before Changing

修改常量、枚举、结构或资源槽之前，先使用 `rg` 搜索名称和值，并检查以下镜像：

| Contract | Primary locations |
|---|---|
| `InkPoint` layout | `draw3/renderer.cppm`, `ink.hlsli`, structured buffer creation |
| `HighlighterPrimitive` layout/sweep | `draw3/renderer.cppm`, `ink_prediction.cpp`, `ink.hlsli`, both shaders |
| Global shader constants | `draw3/renderer.cpp`, `ink.hlsli`, VS/PS binding calls |
| L0/L1/L2 semantics | `drawing_controller.cpp`, `renderer.cpp`, pixel shader；phase document 只记录历史设计/计划，差异须单列 |
| HLSL source/output | `.vcxproj`, `.rc`, `resource.h`, `.hlsl/.hlsli`, `.cso` |
| Transparent fallback | `transparent_presentation.cppm`, `transparent_presentation.cpp` |
| Input controls | `window_control.cppm`, `window_control.cpp`, `README.md` |

## Existing Helpers to Reuse

- 矩形：`UnionRectInPlace`、`IsEmptyRect`、`ClampRectToCanvas`、`GetFullCanvasRect`、`RectFromStrokePoints`。
- 操作层：`SetOperatorTarget`、`ClearOperatorLayer`、`ApplyOperatorLayers`。
- 资源生命周期：`CreateSizeDependentResources`、`ReleaseSizeDependentResources`、`ReleaseAttempt`。
- 日志：`LogHResult`、`LogWin32Error` 以及 DWM/DXGI 诊断函数。
- 荧光笔几何：`BuildHighlighterGeometry` 和其中统一的 CPU bounds 计算。

新增同类工具前先确认现有帮助函数是否已经编码了裁剪、padding、单位操作或回退语义。

## Do Not Force Reuse Across Different Semantics

- 普通笔/橡皮的圆胶囊点流与荧光笔 primitive 流使用不同 GPU 缓冲区；不要只为减少类型数量而合并。
- GPU transparent present 与 ULW CPU readback 的 alpha/命中测试语义不同；不要共享会污染内部画布的背景处理。
- `.vcxproj` 对 `main2.cpp`、`main3.cpp`、`renderer2.h`、`shader.hlsl` 仅保留 `None` 引用且当前文件不存在；`ResTest/` 是独立参考解决方案。不得把这些残留引用或参考代码当成可直接复用的当前实现。
- 第三方 `additional/` 和 `HiEasyX/` 的既有模式不自动成为 `draw3` 新代码的风格标准。

## Review Checklist

- [ ] 搜索过被修改的名称和值。
- [ ] 所有 CPU/GPU 镜像已列出并同步。
- [ ] 没有重新实现现有矩形、操作层或日志帮助函数。
- [ ] 抽取没有把语义不同的工具或 presenter 强行合并。
- [ ] README、架构说明和 Spec 中的稳定规则已同步。

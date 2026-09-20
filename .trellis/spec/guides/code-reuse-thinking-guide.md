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
| UI3 Bar 按钮行为 | `native-desktop/rendering-and-ui.md` 的 “PageControl 与 Bar 按钮单一来源合同”；Main Bar 与跨 HWND PageControl 必须调用同一运行时，独立资源不得复制行为 |
| UI3 即时内容事务 | `ApplyBarImmediateContentUpdate` + `BarUiWordClass::SetStringImmediate`；取消旧关键帧、替换 current/target/pending 和恢复 scale/opacity 必须在同一事务 |

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
- 第三方 `additional/` 的既有模式不自动成为 `draw3` 新代码的风格标准。
- 复用背景 draw/Shape 不表示背景与按钮拥有相同光强语义。Main Bar 与 PageControl 外框保持 Shape 默认 `frameCursorLightIntensityScale = 1.0`；`BarButtonCursorLightIntensity` 只属于按钮，不能套到背景后再以补偿常量提亮。
- 内容更新策略按具体内容收窄：Page 数字使用即时事务并立即处理空文本可见性；Arrow/Add SVG 和语义标签仍使用 Animated 中点转换。禁止用一个 surface 级“关闭动画”开关同时改变两者。
- PPT 空白拖动区必须复用当前动画背景 Shape 的圆角 hit；透明 presentation margin、圆角外像素和第三光源 diffuse 不是背景命中区。

## Review Checklist

- [ ] 搜索过被修改的名称和值。
- [ ] 所有 CPU/GPU 镜像已列出并同步。
- [ ] 没有重新实现现有矩形、操作层或日志帮助函数。
- [ ] 跨 HWND 复用 UI3 Bar 按钮时，Main Bar 与消费者调用同一 layout/interaction/advance/draw/hit/damage 入口，而不是只共享常量、类型或底层绘图原语。
- [ ] 背景/按钮光强语义与 Immediate/Animated 内容策略没有因共享底层类型而被错误合并。
- [ ] 抽取没有把语义不同的工具或 presenter 强行合并。
- [ ] README、架构说明和 Spec 中的稳定规则已同步。

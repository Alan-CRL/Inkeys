# 几何展开面板触摸准入回归

## 根因
- `BarUISetClass::PreparePresentedButtonHits` 已为新几何面板退役 `GeometryAttributeBar` 至 `GeometryAttributeBar_Close` 的透明控件，构造 `visualHits` 时明确令它们不可见。
- 同一函数的触摸白名单仍仅调用这些旧控件的 `AddShape/AddShapes`，没有加入独立 `geometryAttribute.frame_`。因此 `ProbeTouchScreen` 在整个新面板返回 Miss，原生 Touch router 不会将 Down 交到 worker；主栏和鼠标业务入口仍可工作。
- 这是 dev 的已呈现触摸白名单与 feature/shape 独立面板集成遗漏。已有几何测试直接调用 `Pointer`，跳过原生准入，无法暴露此问题。

## 最小修复
- `Bar.Button.cpp` 从本次实际成功呈现帧的 `geometryAttribute.frame_` 建立 Rigid 面板区域，替换旧几何白名单；只在当前几何工具、展开、非折叠且 frame.acceptsInput 时 Accept，否则可见退场面板 Blocked。
- `Bar.Main.cppm` 为 owner 提供 friend 访问，沿用现有 eraser/recall 的同帧收集方式，不在窗口线程读取可变动画状态。Prepare/Publish 与 bottom-dock serial 仍按原逻辑发布；按钮细粒度命中由 GeometryAttributePresentation 裁决。
- `Bar.GeometryAttribute.Test.cpp` 在真实场景离屏成功提交后，以屏幕坐标通过 `ProbeTouchScreen` 检查每个可见可用按钮，包括类型、图形、样式和关闭，覆盖原多语言/DPI/上下停靠矩阵。原有 Pointer 点击/禁用行为测试保留。

## 验证
本代理完成静态 diff --check；完整 ARM64 构建、离屏几何测试和输入 worker 测试由根代理统一执行。未运行交互 GUI、Computer Use，未提交。

# Cross-Layer Thinking Guide

本项目的主要风险不在传统前后端边界，而在 Win32 回调、主绘制循环、墨迹模型、D3D11 资源、HLSL 和透明呈现之间。

## Map the Full Path

修改前先写出受影响路径：

```text
Win32/HiEasyX message
  -> WindowController atomic request
  -> main / DrawingController
  -> Stroke Modeler and ink_prediction
  -> InkRenderer CPU buffers and operator layers
  -> HLSL coverage/operator output
  -> L2/backbuffer
  -> DirectComposition/DWM/ULW presenter
```

对每条边界确认：

- 数据由谁拥有，何时失效。
- 坐标、半径、时间、alpha 是否仍保持相同单位和语义。
- 失败是返回 `false`、记录日志、回退，还是请求下一次全量呈现。
- resize、清屏、抬笔和呈现失败是否仍能恢复一致状态。

## Required Boundary Checks

### Win32 callback to drawing thread

- 窗口过程只写原子请求和待处理尺寸；D3D 资源重建留在主绘制线程。
- 发布 payload 后再以 release 写请求标志，消费端以 acquire 读取请求后再读取 payload。
- 不在窗口回调中直接操作 `InkRenderer` 或交换链。
- RTS 多点同时检查 HWND Tablet Pen Service 属性、`WM_TABLET_QUERYSYSTEMGESTURESTATUS` 返回值和 `IRealTimeStylus3`；不能只验证 COM 初始化成功。

依据：`WindowController::HandleWindowMessage`、`ConsumeResizeRequest`、`DrawingController::ProcessPendingResize`。

### Model to render points

- 原始鼠标速度只用于普通笔宽估算，预测点继承最后真实笔宽。
- 最新 snapshot 覆盖采样改变了速度采样节奏；每份真实速度只滤波一次，第一份速度不得回写已可见起笔，半径仍需时间/距离双限速。
- `ActiveMouseStroke` 的提交游标必须单调前进，已进入 L1 的稳定前缀不能重复提交。
- Up 收尾由 `retainPredictionOnUp` 唯一选择：默认连接 `kUp` 的真实 modeled 尾段并清除 prediction；开启时只烘干上一帧可见 L0，禁止两种尾段同时绘制。
- 荧光笔的 12px 起止方向窗口、重复点阈值和 dirty bounds 必须一起检查。

依据：`AppendNewModeledPoints`、`RebuildPredictedPoints`、`CommitStablePrefixToL1`、`BuildHighlighterGeometry`。

### CPU to HLSL

- C++ 结构大小、字段顺序、常量缓冲区对齐、寄存器槽和 primitive 枚举必须同步。
- `GlobalShaderConstants` 要同时绑定 VS `b0` 和 PS `b0`。
- SRV 用作 RTV 前必须解除绑定。

依据：`renderer.cppm`、`renderer.cpp`、`ink.hlsli`。

### Layer composition

- L2 是已落定的 premultiplied RGBA 画布。
- L1 是当前笔稳定前缀操作，L0 是每帧清空重绘的实时操作。
- 普通绘制与橡皮都编码为 `Add + Retain * Below`；同笔分段默认使用覆盖率并集。
- 抬笔时默认把模型 `kUp` 的真实尾段并入 L1；`retainPredictionOnUp` 开启时改为把最后可见 L0 并入 L1，再一次性作用到 L2。
- “保留最后可见 L0”表示直接烘干其真实尾部、prediction 和笔锋；此分支不得再并排绘制或重建另一条终态尾部。

依据：`DrawingController::CompositeLayersToBackBuffer`、`DrawMouseStroke`、`InkRenderer::ApplyOperatorLayers`。

### Presenter fallback

- 默认初始化顺序是 DirectComposition、DWM extended frame、ULW；已知 QCOM ARM64 适配器优先 ULW，以规避 DComp 初始化成功但透明像素实际显示为黑色。
- 每次新模式尝试前清理上一模式的 presenter、renderer 和 swapchain 状态。
- GPU 路径保留真透明 alpha；仅 ULW CPU 输出副本叠加 `1/255` alpha 命中测试底层。
- presenter 初始化成功只证明 API/资源链可用，不证明桌面合成后的可见 alpha 正确；新增适配器或呈现路径时必须在真实桌面背景上验证透明结果。

依据：`TransparentPresentationController::Initialize`、`Impl::ReleaseAttempt`、`UlwDirtyRectPresenter::Present`。

## Review Questions

- 变更是否跨越了 `.cppm` 公开契约和 `.cpp` 实现？
- 是否存在同一个值在 C++、HLSL、`.vcxproj`、`.rc` 和说明文档中的镜像？
- 当前源码与阶段说明是否存在差异，且差异是否被显式标记而非自动裁决？
- 局部 dirty rect 是否覆盖旧内容清除区和新内容绘制区？
- resize 后 L2 和 L1 是否保留左上角交集，L0 是否可安全重建？
- 失败回退是否释放了所有上一尝试的资源？
- 呈现路径是否只验证了 HRESULT，还是也验证了真实桌面上的透明 alpha？
- 兼容性描述是在陈述项目目标、已有代码路径，还是有环境记录的实测能力？
- 数据离开瞬时 L2 视觉画布进入持久化时，prediction 是否已经被真实采样替换或显式确认？
- 人工测试是否覆盖普通笔、荧光笔、橡皮、短划、静止、抬笔、resize 和透明模式回退？

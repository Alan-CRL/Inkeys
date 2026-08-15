# Cross-Layer Thinking Guide

本项目的主要风险不在传统前后端边界，而在 Win32 回调、主绘制循环、墨迹模型、D3D11 资源、HLSL 和透明呈现之间。

## Map the Full Path

修改前先写出受影响路径：

```text
Win32 window message
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
- resize、撤回/页面命令、抬笔和呈现失败是否仍能恢复一致状态。

## Required Boundary Checks

### Win32 callback to drawing thread

- 窗口过程只写原子请求和待处理尺寸；D3D 资源重建留在主绘制线程。
- 发布 payload 后再以 release 写请求标志，消费端以 acquire 读取请求后再读取 payload。
- 不在窗口回调中直接操作 `InkRenderer` 或交换链。
- RTS 多点同时检查 HWND Tablet Pen Service 属性、`WM_TABLET_QUERYSYSTEMGESTURESTATUS` 返回值和 `IRealTimeStylus3`；不能只验证 COM 初始化成功。
- 输入被手势状态机抑制时，沿 `RTS contact -> DrawingController -> WM_POINTER cursor/haptics` 检查所有副作用；“不产生 Stored Stroke”不等于已抑制接触光标、系统光标决策和触觉预启动。跨线程抑制需锁存到对应设备终态，并覆盖 Pointer 与 RTS 任一路径缺失的设备。
- contact Down 的队列顺序与已有 contact 的合并 snapshot 是两条时间线；处理新 Down 前检查旧单指是否已在 mailbox 发布较早的 `Up/Cancelled`，只退休手势归属，不能跳过旧笔画正常收尾。
- 平移触点拓扑变化时同时核对几何 centroid、每指估速位置和 QPC 零点；只重建其中一项会把触点加入/移除误算成速度尖峰。

依据：`WindowController::HandleWindowMessage`、`ConsumeResizeRequest`、`DrawingController::ProcessPendingResize`。

### Model to render points

- 原始鼠标速度只用于普通笔宽估算，预测点继承最后真实笔宽。
- 最新 snapshot 覆盖采样改变了速度采样节奏；每份真实速度只滤波一次，第一份速度不得回写已可见起笔，半径仍需时间/距离双限速。
- 活动 contact 的提交游标必须单调前进，已进入 L1 的稳定前缀不能重复提交。
- Up 收尾是否只从确认真实点生成 Stored Stroke；Pen 是否烘入 taper、去重连接点并排除 prediction/time。
- 荧光笔固定矩形的 8:1 half size、0.25px 去重、sweep coverage 和 dirty bounds 必须一起检查。

依据：`AppendNewModeledPoints`、`RebuildPredictedPoints`、`CommitStablePrefixToL1`、`BuildHighlighterGeometry`。

### CPU to HLSL

- C++ 结构大小、字段顺序、常量缓冲区对齐、寄存器槽和 primitive 语义必须同步。
- `GlobalShaderConstants` 要同时绑定 VS `b0` 和 PS `b0`。
- SRV 用作 RTV 前必须解除绑定。

依据：`renderer.cppm`、`renderer.cpp`、`ink.hlsli`。

### Layer composition

- L2 是已落定的 premultiplied RGBA 画布。
- L1 是当前笔稳定前缀操作，L0 是每帧清空重绘的实时操作。
- L0 是多工具共享层；引入“稳定内容保留 L0”优化时，必须同时检查其他活动 contact 是否仍需刷新，并在任何共享层重建后重放全部仍活动内容。
- 普通绘制与橡皮都编码为 `Add + Retain * Below`；同笔分段默认使用覆盖率并集。
- 抬笔时先把最终 Stroke 追加到当前 Canvas，再从刚追加的对象重建 operator 几何；不得先从 ActiveStroke 绘制再保存另一份数据。
- 同帧多个完成 Stroke 必须按 Canvas 追加顺序独立作用到 L2；共享 MAX/MIN coverage 会破坏半透明和擦除顺序。
- 每笔首次 L2 提交前必须先追加 RenderItem；热前像在 L1 栅格后、L2 resolve 前捕获，冷撤回只在受影响 tile 构建候选画面，成功后才提交 visibility。

依据：`DrawingController::CompositeLayersToBackBuffer`、`DrawStoredStroke`、`InkRenderer::ApplyOperatorLayers`。

### Presenter fallback

- 窗口创建链必须按 `ShouldPreconfigureNoRedirectionBitmap -> WindowController 独立窗口线程 -> CreateWindowExW -> DComp ConfigureWindow` 检查；不能只看 presenter 初始化阶段。
- `WS_EX_NOREDIRECTIONBITMAP` 是 DComp 的创建期窗口契约。创建后缺失时记录并回退，不用 `SetWindowLongPtr` 补设。
- 修改窗口线程、创建样式或 `.vcxproj` 时必须比较 Debug/Release，并验证初始化事件、`GWLP_USERDATA` 路由和关闭等待都能完成。
- 所有适配器统一按 DirectComposition、DWM extended frame、ULW 初始化；厂商、架构或 OS 标签本身不能改变顺序。
- 每次新模式尝试前清理上一模式的 presenter、renderer 和 swapchain 状态。
- GPU 路径保留真透明 alpha；仅 ULW CPU 输出副本叠加 `1/255` alpha 命中测试底层。
- presenter 初始化成功只证明 API/资源链可用，不证明桌面合成后的可见 alpha 正确；新增适配器或呈现路径时必须在真实桌面背景上验证透明结果。
- 设备/驱动专用兼容策略必须同时记录 OS、VendorId、DeviceId、SubSysId、Revision、UMD driver version 和真实背景视觉结果，不能把单机现象泛化为整个厂商。

依据：`TransparentPresentationController::Initialize`、`Impl::ReleaseAttempt`、`UlwDirtyRectPresenter::Present`。

## Review Questions

- 变更是否跨越了 `.cppm` 公开契约和 `.cpp` 实现？
- 是否存在同一个值在 C++、HLSL、`.vcxproj`、`.rc` 和说明文档中的镜像？
- 当前源码与阶段说明是否存在差异，且差异是否被显式标记而非自动裁决？
- 局部 dirty rect 是否覆盖旧内容清除区和新内容绘制区？
- resize 后 L2 和 L1 是否保留左上角交集，L0 是否可安全重建？
- 失败回退是否释放了所有上一尝试的资源？
- 呈现路径是否只验证了 HRESULT，还是也验证了真实桌面上的透明 alpha？
- 窗口创建参数是否在 Debug 与 Release 都实际进入 `CreateWindowEx`，而不是只在调用前写入了预设？
- 兼容性描述是在陈述项目目标、已有代码路径，还是有环境记录的实测能力？
- 数据离开瞬时 L2 视觉画布进入持久化时，prediction 是否已经被真实采样替换或显式确认？
- 人工测试是否覆盖普通笔、荧光笔固定单点/极慢移动、橡皮、静止、抬笔、resize 和透明模式回退？

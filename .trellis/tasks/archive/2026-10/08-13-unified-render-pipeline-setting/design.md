# Design

## Architecture

`Inkeys.UI.RenderPipeline` 吸收 `IdtD2DPreparation.*` 与 `RenderScheduler`。模块内部由一个共享状态对象拥有 factory/device/font 资产、客户端表、唤醒事件、运行标志和渲染线程。`Initialize` 完成共享资产与线程创建，`Shutdown` 先阻止新请求并同步注销客户端，再唤醒/等待线程，最后逆序释放资源。

每个注册客户端实现单帧回调。管线按 Bar、PPT Main、PPT Previous、PPT Next、PPT Page、PPT End、Settings 的固定顺序处理本帧活动位。活动位来自显式请求、上一帧 `Continue` 或到期的 `Retry`，未活动客户端不调用。

## Shared Graphics Assets

- D2D 1.1 factory、DWrite factory/custom font collection 生命周期属于管线。
- D3D11 device 默认 WARP，请求 FL11.1/11.0，并只在 `E_INVALIDARG` 时用 FL11.0 重试。
- 管线保存 D3D11 device、可选 device1、immediate context、DXGI device/factory、D2D device、backend、feature level 和单调递增 generation。
- Bar/PPT 的 D2D context、target bitmap、GDI interop 仍由客户端按 epoch 管理。
- Setting 从共享 D3D device 创建传统 discard swap chain，并独占 RTV、ImGui renderer 资源和图片 SRV。

## Frame Contract

`FrameContext` 在一次回调期间只读，提供 epoch 快照、共享 COM 资产和统一帧时间。客户端不缓存 context 指针，不发布 generation，也不从非渲染线程调用 immediate context。

`FrameResult`：

- `Idle`：移除连续位，等待下次显式请求。
- `Continue`：下一节拍继续调用，仍受 16,666,667 ns 上限约束。
- `Retry`：只保留当前客户端重试，不影响其他客户端。
- `DeviceLost`：仅由管线线程恢复共享设备、发布新 epoch，并标记全部已注册客户端。
- `Stop`：仅用于进程级管线退出，不作为窗口隐藏语义。

## Wakeup And Pacing

`Request` 原子 OR 客户端位并设置 manual-reset event。渲染线程取位后 reset event，再执行一次交换以覆盖 reset 边界请求；连续位与新请求合并后按固定顺序执行。存在连续客户端时等待到统一帧截止时间，全部 idle 时无限等待。

## Setting Session

Setting 窗口线程负责 Win32 生命周期、capture/cursor/IME 和消息归一化。持久 `SettingSession` 由渲染线程拥有 ImGui context、DX11 backend、swap chain、RTV、纹理和一帧绘制。窗口线程与渲染线程通过专用互斥量串行访问 ImGui IO 所需状态，窗口线程不执行 renderer/backend 绘制调用。

显隐状态通过 `Show/Hide/Toggle/IsVisible` 管理。首次可见帧创建会话资源；resize、occlusion 和 epoch 变化在单帧状态机中处理。隐藏时逆序释放会话资源并返回 `Idle`。

单一 FIFO 业务 worker 执行可能阻塞的文件、配置、更新、Shell、确认和重启动作。渲染回调只入队命令和读取结果快照；worker 完成后发布结果并请求 Settings。

## Assets And Compatibility

ImGui HLSL 与 CSO 迁入 RenderPipeline 资产目录，RC 使用新路径，backend 继续从资源加载 CSO。Win7 SP1 + KB2670838 仅依赖 D2D 1.1、基础 D3D11 device/context、传统 DXGI factory/swap chain 与 discard 呈现；device1、FL11.1 均为可选能力。

## Migration And Rollback

先建立 RenderPipeline 并迁移现有 Bar/PPT，再接入 Setting，最后删除旧文件与工程项。每一步保持完整 Solution 可构建。若 Setting 接入失败，可在删除旧循环前回滚到独立 Setting 路径；删除旧资产前必须完成静态引用审计。

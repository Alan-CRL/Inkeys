# 公共图形资产与 Setting 统一渲染

## Goal

将 UI3 Bar、五个 PPT 窗口和 Setting 接入同一套 D2D 1.1 / D3D11 WARP 图形资产与唯一串行渲染线程，在保持 60 FPS 上限、按客户端唤醒和设备恢复语义的前提下，移除重复设备与独立 Setting 绘制循环。

## Background

- 本任务是 `08-01-render-pipeline-refactor` 的实现子任务；`08-12-pptcom-ui3-multi-window-rendering` 保持独立。
- `07-17-investigate-unified-ui-d3d11-pipeline` 的调查结论并入本任务：共享 device/immediate context，窗口保留独立呈现资源，ImGui 使用内嵌预编译 CSO，Win7 不依赖 D3D11.1 可选接口。
- 当前 `IdtD2DPreparation.*` 拥有共享 D3D/D2D/DWrite 资产和 generation 恢复，`RenderScheduler` 拥有串行调度；Setting 仍有独立 hardware D3D11 device 和 24 FPS 循环。

## Requirements

- R1：将 `IdtD2DPreparation.*` 和 `RenderScheduler` 合并为 `Inkeys.UI.RenderPipeline`，由模块统一管理共享图形资产、设备 generation、唯一渲染线程、60 FPS 节拍和客户端唤醒。
- R2：默认创建共享 WARP device，请求 `FL11_1, FL11_0`；旧运行时仅在 `E_INVALIDARG` 时退回 `FL11_0`。`ID3D11Device1` 可选，不作为 Win7 前置条件。
- R3：共享 D3D11 device/immediate context、DXGI/D2D device、D2D 1.1 和 DWrite factories/font collection；Bar/PPT 保留各自 D2D context/target/GDI interop，Setting 保留独立 discard swap chain/RTV。
- R4：保持 Win7 SP1 + KB2670838 静态兼容路径，使用传统 `IDXGIFactory::CreateSwapChain` 和 discard swap effect，不引入 DirectComposition、flip model 或其他 Win8+ 呈现依赖。
- R5：删除旧全局 `d3dDevice_UI3`、`d2dDevice_UI3` 等变量和 `IdtD2DPreparation.*` 工程项；消费者导入新管线模块，仅借用 DX 类型的代码直接包含 SDK 头。
- R6：将 ImGui HLSL/CSO 移入渲染管线资产目录并更新 RC/工程引用，继续加载现有内嵌预编译 Shader，禁止恢复运行时 `D3DCompile`。
- R7：管线客户端合同为 `RenderFrame(FrameContext) -> FrameResult`；上下文只读提供当前 epoch、共享资产和统一帧时间，结果保留 `Idle/Continue/Retry/DeviceLost/Stop` 语义。
- R8：固定串行顺序为 Bar、五个 PPT 客户端、Settings。每帧只调用请求位或上帧返回 `Continue/Retry` 的客户端；局部失败只重试该客户端。
- R9：请求继续使用原子位合并和 manual-reset event，并保留 reset 后二次交换；连续帧统一受 `16,666,667 ns` 上限约束，所有客户端 idle 时无限等待。
- R10：共享设备丢失只允许渲染线程发布新 epoch，随后请求全部已注册客户端重建各自资源。
- R11：Setting 重构为持久会话和单帧入口。显示且可呈现时返回 `Continue`，隐藏后释放会话资源并返回 `Idle`；resize、隐藏、generation 变化和退出按逆序释放/重建。
- R12：Setting WndProc 保留在 HWND 所属线程执行捕获、光标、IME 和生命周期操作，并通过专用同步边界串行更新 ImGui IO；ImGui context、DX11 backend、绘制与 Present 仅由渲染线程使用。
- R13：Setting 的文件、配置持久化、更新、Shell、模态确认和重启等可能阻塞操作进入单一 FIFO 业务 worker，结果以线程安全快照发布并请求 Settings。
- R14：Setting 对外提供 `Initialize/Shutdown/Show/Hide/Toggle/IsVisible/WindowProc`，替代生产路径对 `test.select` 的直接显隐读写。
- R15：退出顺序为停止生产者并同步注销客户端，再停止管线线程、窗口服务和共享图形资产。

## Acceptance Criteria

- [x] Bar、五个 PPT 客户端和 Setting 使用同一共享 WARP device/immediate context，并由唯一管线线程串行绘制。
- [x] Setting 不再创建独立 hardware device 或运行 24 FPS 长循环；可见且可呈现时以统一 60 FPS 上限连续绘制，隐藏后可使管线进入无限等待。
- [x] 未请求且未连续运行的 Bar/PPT 客户端不会被扫描或计算；固定顺序、局部 Retry 和设备丢失全客户端恢复均有无窗口测试。
- [x] 请求位合并、并发请求不丢失、同步注销和退出唤醒均有无窗口测试。
- [x] Setting 显隐、resize、occlusion、generation 切换、资源重建和业务完成通知有纯状态测试。
- [x] 无 HWND 的 WARP 初始化检查验证 FL11.0+、immediate context、DXGI/D2D/DWrite 资产有效。
- [x] `IdtD2DPreparation.*`、旧图形全局及旧模块 import 从产品工程和活动消费者中移除；ImGui CSO 由新资产路径嵌入且没有运行时 `D3DCompile`。
- [x] ARM64 host MSBuild 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64`，并通过 `InkeysHeadlessTests.exe --no-window`、`git diff --check` 和静态引用审计。

## Constraints

- 只覆盖 Bar、五个 PPT 窗口和 Setting；不扩展到画板、定格、白板或业务 UI 重做。
- 不改变现有配置格式、Setting 窗口样式或 ImGui 视觉内容。
- 不启动 Inkeys、不运行会创建 HWND 的测试、不操控桌面，不执行 GUI/Office/WPS 验收。
- Win7 仅做静态兼容审计，不宣称完成真实 Win7 运行验证。
- 保持原文件编码与 CRLF，仅在关键设备、调度、唤醒和线程边界添加简短中文注释。

## Out of Scope

- UI2/历史 `IdtFloating` 路径恢复或改造。
- Draw2/Draw3、画板、白板、定格和背景窗口接入。
- Setting 业务功能、配置结构、窗口样式或控件视觉重做。
- DirectComposition、flip model、D3D12 或新第三方渲染依赖。

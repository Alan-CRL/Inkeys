# DWM 透明 presenter 禁用：独立 diff 与调用链复审

审查时点：2026-09-27。范围仅为工作区 `Draw3.TransparentPresentation.cpp/.cppm` 和 `Draw3.HiddenWindowTest.cpp` 的本次改动及其直接调用链。未改生产代码；未启动 HWND/GUI 测试或并发构建。当前代码与 H0 历史问题分别标记。

## 已核实的改动

- `Draw3.TransparentPresentation.cpp:90-93` 的自动候选精确为 `DirectCompositionVisualTree`、`UlwDirtyRect`。`Initialize:783-796` 与 `RecoverFromRuntimeFailure:862-875` 复用该数组；`allowDirectComposition=false` 时自动模式仅遍历 ULW。`Host::Start:1071-1082` 把宿主强制模式映射到 `TransparentPresentationOptions`。
- `Impl::TryInitialize:668-680` 是初始化强制/自动选路和恢复两分支的共同入口；DWM2、DWM 与未知 enum 都在 `ConfigureWindow` 和 swap-chain 创建前拒绝。DComp API 缺失也在样式回调之前跳过，适用于 Win7 无 `dcomp.dll` 的自动 ULW 路径。旧 DWM enum、映射和 presenter 实现仍在二进制源码中，但当前正常入口不可到达。
- `CreateSwapChain:614-641` 保留 `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`，没有引入其他 swap effect。`GraphicsInitialization.cpp:49-99` 先试 HARDWARE 的 11.1/11.0，遇到旧系统 `E_INVALIDARG` 重试仅 11.0，硬件失败再试 WARP 的同一列表；无 FL11.0 硬件但 WARP 可创建 11.0 时理论上走 WARP。没有 FL11.0 的 HARDWARE/WARP 都失败时启动失败，不能声称支持 FL10-only 系统。用户报告 Win7 SP1+仅 KB2670838 实测 FLIP 可用，本轮未复现，故不依据相反的微软通用说明添加 bitblt 回退。
- `DirectCompositionPresenter::Initialize:369-375` 通过绝对 System32 路径动态加载 `dcomp.dll` 并解析导出；当前 ARM64 Release 二进制（该 diff 之前构建）的 `dumpbin /imports` 未列出 `dcomp.dll`，列出 `d3d11.dll`、`dwmapi.dll` 和 `UpdateLayeredWindowIndirect`。该导入证据不能代替 Win7 x86/x64 最终产物的导入表核验。
- 新隐藏窗口测试 `RunMode:871-881` 限制自动结果只在 DComp/ULW 内；`RunHiddenWindowIntegrationTest:2177-2197` 对两种强制 DWM 期望启动失败、宿主停止、样式回调不增加，并随后测试强制 ULW。`git diff --check` 无错误。测试未运行，所有运行时结果仍为“未验证”。

## 审查发现

### R-DWM-01：DComp 运行期降级仍可能失去 Draw3（H0 已有，条件性严重）

`TransparentPresentationController::RecoverFromRuntimeFailure:820-876` 在原 `primaryWindow` 上重试 DComp，然后尝试 ULW；`ConfigureWindow:530-534` 要求清除 `WS_EX_NOREDIRECTIONBITMAP`。Window Service 的 `SetExtendedStyleFlags` 在 `Window.cpp:1547-1564` 写后核对；隐藏测试 `Draw3.HiddenWindowTest.cpp:2135-2152` 已明确覆盖“DComp HWND 创建期样式不可清除时 ULW 启动失败”的条件。运行期无对应 HWND 重建：`DrawingController::Run:6309-6321` 在所有恢复失败后 `RequestExit()` 并退出绘制循环；`IdtMain.cpp:1910-1930` 只有**启动时**才 StopProduct→StopAndJoin→重建 legacy HWND→StartProduct，`IdtMain.cpp:2047-2105` 的正常循环未监测/重启运行期 Draw3。结果是持久性 DComp 故障且样式不可清除时，绘图输入服务结束，不能靠 ULW 自动恢复。触发条件在当前真机未复现，不能记成已发生的用户故障；代码条件路径已确认，需故障注入与目标硬件验证。若可触发，应作为首发输入失效阻塞。

现成的 `Window::Service::Destroy/Create`（`Window.cppm:134-135`）通过 owner thread 命令执行，但 `Window.cpp:1838-1862` 的 `CanChangeChainRole` 在 Drawpad 下仍有 Bar/PPT popup 时拒绝销毁 Drawpad；Presentation 又依赖 Drawpad，且 `Window.cpp:850-863` 定义整条 owner 链。`StopAndJoin`/`Start` 可重建整组 HWND，但会同时销毁 Bar/PPT/Setting 等窗口并触发其生命周期回调（`Window.cpp:1017-1039`），不能直接当作绘图线程局部恢复。绘图线程也不能同步调用 `StopProduct()`：`Host::Stop:1271-1325` 会 join 自身 `drawingThread`。最小安全方向是在绘制线程只发布带 generation 的“需主线程重建 legacy Host”状态并停止接收输入，由主线程协调 RTS/Draw3、Bar/Setting/窗口 owner 清理与完整重建，重新发布当前业务快照并验证首个成功 Present；保留最后有效 UInk，不在损坏的绘制线程或 `Window::Service` 命令回调内做同步重建。此方案涉及多模块生命周期，当前 diff 不宜顺带实现；需要独立设计、故障注入和手工验收。若暂不能完成，发布门禁保留“DComp 运行期 persistent failure 后输入失效”风险。

### R-DWM-02：强制 DWM 拒绝不发生在所有 HWND 副作用之前（本次测试合同缺口）

`Host::Start:997-1038` 在进入 `TransparentPresentationController::Initialize` 之前先 `WindowController::AttachExternal`；后者在 `Draw3.WindowControl.cpp:266-298` 对外部 HWND 调用 `SetProp(MICROSOFT_TABLETPENSERVICE_PROPERTY, ...)`，Host 也会启动 AutoSave worker。新测试只比较 `styleContext.callCount`，可证明没有走样式回调，不能证明没有任何 HWND 副作用。失败清理会 `DetachExternal`/`RemovePropW`，所以这主要是早拒绝合同与故障路径效率问题，正式产品自动模式不受影响。若要求“强制 DWM 在一切窗口副作用前拒绝”，应在 `Host::Start` 的初始检查、`bridge.Reset`/AttachExternal 之前预检两种禁用 `requiredPresentationMode` 并返回 false；保留 `TryInitialize` 中的门禁防止绕过与恢复；测试应检查真实 `GetProp` 和 exStyle 前后不变。当前状态：静态确认，运行未验证。

### R-DWM-03：隐藏窗口套件不能直接证明 Win7 ULW 目标（既有测试限制）

`RunHiddenWindowIntegrationTest:2087-2134` 无条件先创建带 `WS_EX_NOREDIRECTIONBITMAP` 的 DComp-compatible HWND，并要求自动和强制 DComp 启动成功；Win7 没有 DComp 导出时强制 DComp 必失败，测试也会记失败。Win7 路径需要独立运行 legacy HWND + 自动/强制 ULW、硬件有/无 FL11.0 与 WARP、DPI/Present/alpha/退出恢复；当前 Win11 ARM64 隐藏测试即使通过，也不能外推到 Win7 SP1+仅 KB2670838 x86/x64。应让隐藏套件按能力探测跳过 DComp 专项但保留 ULW 项，或提供明确的 ULW-only 子模式；不应为通过测试添加 swap-effect 回退。当前状态：静态确认测试结构，目标机与真实 Present 未验证。

## 交付状态

本次二元素选路与 `TryInitialize` 门禁的代码逻辑**静态通过**；未运行的 hidden-window、Win7、FL11.0/WARP、运行期 device-lost/ULW 透明度均为**未验证**。R-DWM-01 是本轮改动之前已有但影响发布结论的恢复风险；R-DWM-02/03 是当前新增验证与目标矩阵的缺口。`IdtMain.cpp:1913` 的启动回退日志仍写“DWM/ULW”，已交主 agent 修正为 ULW。

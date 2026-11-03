# Research: PPT/白板 PageControl 按钮的笔触觉接线

- Query: 分页按钮虽然复用主栏公共按钮/共享交互 worker，是否也能产生笔触觉 Click；PPT 长按重复是否有反馈。
- Scope: internal
- Date: 2026-11-02
- Task: .trellis/tasks/11-02-shape-style-tablet-bar

## Findings

### 结论（静态确认）

**当前 PageControl 的 PPT/白板按钮未接入笔触觉 Click 请求链。** 公共按钮复用覆盖渲染、命中和按压状态机，共享 worker 覆盖有界消息分派；两者均不会自动创建笔接触凭据或在动作接受后补发触觉。结论来自完整分派和业务链核对，不是仅因 PageControl 文件没有 haptic 字样。

### PageControl → 共享 worker → BarPageSurface → 业务链

1. `Inkeys/Inkeys/UI/PageControl/PageControl.cpp:4539` 为各分页 HWND 注册 `PageControlInputEvent / PageControlInputTick / PageControlInputCancel`；使用 `Inkeys::UI::Bar::RegisterInteractionSurface`，不是主栏协程的主窗口消息入口。
2. `PageControl.cpp:3116` producer 构造 `SurfaceInputEvent`：携带原始 extraInfo、time、screenPoint、generation、touch/contactId、冻结的命中和 captureTicket，然后 `:3131` 附近 SubmitSurfaceInput。**没有笔 Haptics::Click 凭据字段或构造步骤**。
3. `Bar/Bar.InteractionWorker.h:163` 完整 SurfaceInputEvent 只有上述输入/命中/捕获数据，`:186` handler 签名为 `void(*)(const SurfaceInputEvent&, void*)`；既不携带 ClickSource，也不返回业务接受结果。touch/contactId/extraInfo 不等价于经过核验的当前笔凭据。
4. `Bar.Interaction.cpp:1833` SubmitSurfaceInput 仅确认 registration 并把 event.surface 按值放共享 FIFO；处理 Move 合并和捕获票据，不生成触觉 token。`Bar.Interaction.cpp:1532` PumpSharedInteraction 对 `!event.forMain` 直接 `DispatchSurfaceInteraction(event.surface)`。
5. `Bar.Interaction.cpp:1464` DispatchSurfaceInteraction 校验窗口/registration generation，再在 `:1478` 调原 handler；外层 WithSurfaceInputCallback 只管理 inFlight/异常/取消/回收，不设 AcceptedClickScope、不调用 AcceptBarButtonFeedback，也不检查任何“被消费即震动”的返回值。因此 shared worker **没有隐式补发触觉**。
6. `PageControl.cpp:2334` PageControlInputEvent 执行业务。`:2564` 用 `state.scene.ApplyPointer(Up, frozen hit).clicked` 确认点击身份；`:2593` 起按工作区调用 InvokePptPreview、InvokeDirection 或 InvokeWhiteboardMenu。
7. `Bar.Scene.cpp:857` BarPageSurface::ApplyPointer 只用 ButtonPointerState.Update，保存 hover/pressed，返回 activate/clicked 身份并 Wake 渲染；`:632` 另一坐标版 Pointer 也只更新此状态。`:871` PointerUp 的 bool 参数并不执行业务回调。该 surface **没有触觉发布或 BarButton clickFunc 自动调用**。
8. `PageControl.cpp:780` InvokeDirection 复核当前场景和回调，最终 `:813` callback()；`:816` InvokePptPreview、`:845` InvokeWhiteboardMenu 同样只执行业务。白板回调在 `IdtMain.cpp:3753` 接到 RequestWhiteboardPreviousPage/NextPage/Active/Theme，无笔凭据参数，不能沿下游恢复原接触身份。

### 与主栏已接线链路对比

| 阶段 | 主栏 | PageControl |
| --- | --- | --- |
| 真实笔观察 | `Bar.Interaction.cpp:296` ObserveBarHapticPointer 动态读取 PT_PEN、cursor/source/rawPoint/time；`:2914` 主栏 WndProc 调用 | 分页没有调用该观察入口或建立自己的 ContactTokens |
| Down/Up 关联 | `Bar.Interaction.cpp:3144` BindDown，`:3166` Touch 关联同一接触；不把 TOUCHINPUT.dwID 当 pointerId | 有 UiPointer::Source::Pen 兼容会话不代表有 Haptics ClickSource；SurfaceInputEvent 不带后者 |
| FIFO 传凭据 | `Bar.Interaction.cpp:495` BarQueuedPointerSample 含 `haptic`；`:1119` 随原生样本保存 | 共享 SurfaceInputEvent 无 haptic 字段；worker 不补造 |
| 消费者持有 | `Bar.Interaction.cpp:1325` barPointerHapticSession，`:1378` currentHapticMessage | 分页 Surface 状态只有点击/拖动/场景会话，没有对应触觉会话 |
| 动作已接受 | `Bar.Interaction.cpp:6012`、`:6231` AcceptedClickScope 包围真实主栏回调，再按 buttonActionAccepted 调 AcceptBarButtonFeedback；属性面板也显式报告接受 | BarPageSurface 只返回 clicked；PageControl 的 Invoke* 没有相应接受反馈调用 |
| 发布请求 | `Bar.Interaction.cpp:591` AcceptBarButtonFeedback 验证同一 Down/Up 及 WM_LBUTTONUP，`:597` 调 PublishProductHapticClick | 没有调用此 API，也不会经共享分派进入此主栏私有函数 |

`Bar.Haptics.h:207` AcceptedClick 明确要求有效真实 Up、Down 和 Up 引用相同 ClickSource；同线程 AcceptedClickScope 不是“任何公共按钮天然有反馈”的全局机制。不能把 PageControl 普通 callback 放到主栏同一个 worker 就视为继承当前笔凭据。

### PPT 按下翻页与长按重复

- `PageControl.cpp:2506` ResolvePptDirectionPressPolicy 决定方向按钮按下时触发及长按跟踪；`:2523` invokeDirectionOnDown 直接 InvokeDirection。PPT 的这个接受边沿与主栏“真实 Up 后确认 Click”不同。
- `PageControl.cpp:2686` PageControlInputTick 只在合格 PPT 场景、会话和长按状态下工作，`:2705` 每个 repeat 也只调用 InvokeDirection；没有触觉请求、接触凭据或逐次反馈。
- 白板方向按钮在 `:2596` 的有效 Up 后调 InvokeDirection；白板不进入上述 PPT long-press repeat。
- 因而目前既没有分页普通笔点击的震动，也没有 PPT 每次长按重复的震动接线。若未来加反馈，长按重复要不要反馈、按下还是抬起反馈，是产品行为选择；不能直接套普通按钮 Up 凭据并声称 repeat 也支持。

### 后端仍已存在，缺口在分页前端接线

- `Draw3.Product.cpp:79` PublishProductHapticClick 经产品生命周期门调用 Host；`Draw3.Host.cpp:2928`（调查时当前行号）PublishHapticClick 入独立 ClickMailbox 并 ControlWake。
- `Draw3.DrawingController.cpp:16316` 在绘制 owner 上绑定当前有效笔后播放离散 Click，按钮强度常量遵守既有合同。全 Inkeys 生产源码查询，PublishProductHapticClick 的 UI 调用位于主栏 AcceptBarButtonFeedback；未发现分页业务下游绕过前端直接 PlayDiscrete 的替代链。
- 缺口是：分页窗口笔凭据生成/取消、随 SurfaceInputEvent 的按值凭据传输、分页各真实业务接受点发布请求。仅添加一次 PlayDiscrete 或用“最近笔”会违反既有来源/代际/取消合同；本轮只定位，不设计或实施修复。

### Files found / Related specs / External references

- `UI/PageControl/PageControl.cpp`：producer、共享注册、按钮业务、PPT repeat。
- `UI/Bar/Bar.InteractionWorker.h`：公共 surface 输入载荷和 handler 合同。
- `UI/Bar/Bar.Interaction.cpp`：共享分派及主栏独立触觉接线。
- `UI/Bar/Bar.Scene.cpp`、`Bar.Scene.cppm`：公共分页按钮状态与返回 clicked 身份。
- `UI/Bar/Bar.Haptics.h`：真实接触凭据和接受策略。
- `Drawing/Draw3/Draw3.Product.cpp`、`Draw3.Host.cpp`、`Draw3.DrawingController.cpp`：请求消费后端。
- `.trellis/spec/native-desktop/pen-haptics.md`：仅业务接受点、同一真实笔、Up/有效期/代际/去重、无凭据不反馈，以及可选失败不影响点击。
- 本轮无需外部资料。

## Caveats / Not Found

- 本结论是静态接线审查，未运行 Git/build/GUI/设备测试，未改产品/规范。工作区其他代理可能同时修改代码，后端行号以本次读取为准。
- PageControl 接受笔提升鼠标消息并能翻页，与是否有可安全发送触觉的原始笔凭据，是两件独立事实；不能据前者认定后者存在。
- 主栏有请求接线也不保证所有驱动的 Pointer/Touch 关联、WinRT 能力或实体震感；分页当前缺少请求链则无需先用硬件不支持解释其无反馈。
- 将来的验证至少要区分：前端是否发布、凭据是否匹配、Host是否接纳、后端是否绑定/发送成功与实体是否震动；本轮不扩展硬件型号或修复范围。

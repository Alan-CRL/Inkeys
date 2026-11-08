# Research: Mouse canvas-pan rendering latency

- Query: 鼠标拖动画布高延迟、Touch 正常、背景相对及时；核对导航推进、tile 恢复、可信快照重投影与 Present 之间的生产时序，以及 Mouse 光标 wake 的影响。
- Scope: internal / 只读源码研究；仅写本研究文件，不编译、不运行 GUI、不修改源码、不执行 Git 操作。
- Date: 2026-11-08
- Baseline: 主会话提供 HEAD `4b4edb91`。下列行号来自本轮读取的工作树；未独立运行 Git 验证。

## Findings

### 结论与证据等级

1. **直接确认：Mouse 位置样本额外制造了 interrupt-frame control wake；普通导航 Move 没有这个行为。** 这条旁路既能中断可见 tile 恢复，也能打断导航帧率等待。屏蔽 CanvasPanMode 下位置-only Mouse 光标 render 请求，是范围小且与现有消费需求匹配的修复候选。
2. **直接确认：背景视口在 tile 恢复和 Draw3 Present 前发布。** 因而背景先于墨迹表面响应，与当前线程/呈现分工相容。
3. **直接确认：没有发现 MouseContact 优先分支跳过整帧 Present。** MouseContact 只增加导航前的 ingress drain；活动导航排除了相关 idle `continue`。
4. **重要限制：tile 饥饿本身不能证明所有“墨迹位置长时间落后”。** 已有效且覆盖当前视口的可信快照用当前 viewport 直接计算 UV 重投影，应仍跟手。Mouse 控制 wake 破坏 pacing、可信快照缺失/失去交集、GPU/呈现耗时，是必须区分的后半链路因素。当前用户日志不能独立确认哪一项主导其主观延迟。

### 读取的文件

| 文件 | 用途 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | 实际 Run 循环、导航推进、Freeze 视口发布、恢复抢占、fallback 合成和 Present。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp` | Mouse cursor mailbox、render request 和 control wake 生产者。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp` | 控制 marker、wake generation、普通 Move 与帧 deadline 等待。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RendererSnapshot.cpp` | 可信 L2 快照更新、当前 viewport 重投影和失败返回。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp` | DComp/ULW 呈现、Present1 参数与 swap-chain latency 配置。 |
| `Inkeys/IdtMain.cpp` | 已有生产 probe CLI 路由。 |

### 从样本到两个表面的时序

- `DrawingController.cpp:15401` 的 `updateCanvasNavigation` 调用同一 `CanvasPanDynamics`；视口变化会请求 `viewportRefreshPending`。`contentDelta` 的几何应用与 release velocity 是否为 0 是不同路径。因此日志中的 timing-reset / velocity=0 不能直接当成拖动几何被冻结。
- Run 在 `DrawingController.cpp:18355` 观察 Pen、Mouse mailbox；`18363` 的分支只在导航中选择 `drainIngressBatch()`（`18370`）。`priorityMouseInContact` 不包含独立的 `continue`。
- `DrawingController.cpp:18376` 推进导航，紧接 `18377` 发布白板 viewport，之后才做 tile 恢复。发布 lambda 在 `15373`，按 active/moving/page/generation/x/y 去重（`15395`），以 observer 回调发布（`15397`）。附近注释明确此回调只无锁写入与原子唤醒、不等待 UI3（`15371`）。
- `DrawingController.cpp:18391` 在视口刷新时清 L2，`18401-18403` 重建 plan 并把可见区置为未清晰，`18433` 强制全帧 Present。连续拖动导致每次新视口重建 plan；旧计划进度不能视作连续累计恢复。
- `DrawingController.cpp:18443` 捕获 recovery wake generation；每个 tile 前 `18448-18449` 遇到 `HasPendingWork()` 或任意 wake generation 变化立即 break。
- 可见 tile 的 `RestoreComposition` 在 `18469`，成功才前进 `18492-18495`；失败明确设 `rasterSubmissionFailed`（`18489`），不把失败 GPU 帧当作成功可见回执。
- `navigationActive` 在 `18735-18737` 包含 pan、refresh、recovery。`18745` 和 `18776` 的 maintenance/idle `continue` 条件均要求 `!navigationActive`。因此不能将这些空闲分支解释为“Mouse 拖动时优先输入而从不 Present”。
- `DrawingController.cpp:19896` 的非空 dirty 且 raster 未失败才合成；`19999` 调用 `PresentFrame`，每帧最多一次。`19853` 使 forceFullPresent 对应全屏 dirty。
- 这一顺序允许 Freeze 已消费新视口，而 Draw3 的 tile/reprojection/composition/Present 还在后面。它是“背景快”的源码解释，不能量化现场两者的显示时差。

### Mouse 独有的无效 render-wake 旁路

- `WindowControl.cpp:1147` 的 `PublishMouseCursorSample` 先读取旧值、发布最新 mailbox（`1150-1152`），然后无条件 `RequestDrawingCursorRender()`（`1163`）。valid/contact 边沿另触发 `QueueSystemCursorRefresh()`（`1164-1165`）。
- `WindowControl.cpp:1211-1214` 的 render request 设置原子 flag 后调用 `RequestControlWake()`；`629-632` 将它送到同一 `ContactInputCoordinator`。
- `ContactInput.cpp:380-406` 的 `PublishControlWake` 仅合并队列 marker；即使 `controlWakePending` 已为 true，`386` 仍执行 `SignalWake()`。所以 marker 合并并不等于 wake generation 合并。
- `ContactInput.cpp:467-470` 的 `SignalWake(bool interruptFrame=true)` 默认同时递增 `frameWakeGeneration` 和 `wakeGeneration`。
- 对照普通导航 Move：`ContactInput.cpp:865-868` 把样本追加导航历史；`879-881` 仅消费者已订阅 idle Move wake 时调用 `SignalWake(false)`。活动 Move 保留 mailbox/历史合并，不持续中断帧等待。
- 因此一串 Mouse cursor position-only 更新会在真实 contact Move 之外制造 pending control marker 和 generation 变化；Touch 没有对应的 Mouse cursor 每 Move render 请求。
- Run 从 `18259` 消费 cursor request。Selection/移动时不绘应用主光标，因此该位置-only request 没有应用光标绘制收益，却仍进入上述通用唤醒/抢占协议。

### 比 tile 饥饿更直接的 pacing 影响

- `DrawingController.cpp:20161-20169` 的导航分支按 `target_fps` 计算剩余预算并调用 `WaitForFrameDeadline`。有实际绘制 contact 的帧也在 `20135-20143` 调同一等待函数。
- `ContactInput.cpp:1328-1332` 先检查从上一等待起发生的 `frameWakeGeneration` 变化；若变化立即返回。`1343-1347` 也会在等待中遇到变化时立即返回。
- 因而高频 Mouse cursor control wake 不仅可能让恢复提前 break，也会让 Run 无法按目标帧率稳定等待；其源码影响不依赖 GPU 特定实现。
- **推断，尚未实测：** 更多全屏 clear/replan/composite/Present 提交可能增加 GPU/呈现压力，使输入 consume 很及时而画布显示延迟。必须用 frame/Present 时序确认，不能宣称已观察到无界 GPU 队列。

### 为什么有效快照仍应跟手

- `DrawingController.cpp:19906-19920` 只有可见 L2 已恢复清晰，且可信快照身份/视口变化时才刷新 trusted snapshot。
- `RendererSnapshot.cpp:145-174` 在资源/尺寸/格式有效时 CopyResource 当前 L2，并记录该快照的 viewport；这只是 GPU 提交，不等待像素真正显示。
- 当可见 tile 未完成时，`DrawingController.cpp:19925-19933` 使用 `snapshotCanvas->Viewport()` 调 `CompositeTrustedL2SnapshotToBackBuffer`。
- `RendererSnapshot.cpp:197` 先把当前权威 L2 复制回 backbuffer；`198-201` 可信快照不可用时返回 false；`207-223` 无视口交集时也返回 false。
- 有交集时，`RendererSnapshot.cpp:225-226` 的 sourceOffset 是 **currentViewport - snapshotViewport**。UV 在 `257-260` 由此偏移构造；不对位置做速度积分、低通或追赶动画。motion 归一化（`229-235`）只用于 blur 方向/强度。
- 所以 tile 被频繁抢占时，旧可信快照在其覆盖区域仍应以最新 viewport 移动；受影响的是权威清晰恢复和快照覆盖范围。仅凭“tile 被 break”不能证明快照坐标停留旧帧。
- `DrawingController.cpp:19929` 忽略 fallback composite 的 bool 返回，随后仍继续 operator layers 和可能 Present。无可信快照/无交集可产生缺失内容或部分 L2，当前并不等同于整帧不呈现。此处是诊断缺口；本轮最小修复不应顺带重构其失败语义。

### 呈现后端边界

- `TransparentPresentation.cpp:973-1005` 尝试 waitable swap chain，并在 `994` 调 `SetMaximumFrameLatency(1)`。本文件没有发现对 frame-latency waitable object 的等待调用。
- `TransparentPresentation.cpp:1144-1158` 最终调用 `Present1(0, 0, ...)`。
- `TransparentPresentation.cpp:1401` 后根据 SelectionULW/主 ULW/GPU 模式分支；GPU 在 `1423` 直接 PresentSwapChain，`1429` 的 DwmFlush 是输出交接屏障分支，不是每个普通导航帧的等待。
- 用户日志末尾的隐藏 DrawpadPresentation 输出发生在离开移动模式之后，不能据此确定拖动期间实际使用的分支、每帧 Present 耗时或 DWM 显示滞后。
- 不能因为发现 Present1(0,0) 就断言队列无限增长：waitable 路径的 maximum latency=1 以及不同后端可能限制/阻塞提交，现场需记录实际 backend 和时间。

### 最小修复建议

1. 在 `PublishMouseCursorSample` 现有 mailbox 发布之后，区分 CanvasPanMode 中的 **位置-only** 更新和生命周期/身份边沿。位置-only 更新保留最新样本与所有 contact/navigation 数据，仅跳过不绘应用光标时的 `RequestDrawingCursorRender`。
2. valid/inContact 等系统光标生命周期边沿、Clear、真正工具/模式/owner 改变的请求继续走原 wake。不要在 coordinator 全局降低 control wake 优先级，否则 Down/Up/Command 的紧急语义和 Clear 前后的 fence 会受影响。
3. 绘制模式 Mouse hover / 自绘光标继续原行为；Touch/Pen/RTS 正常导航历史和幂等终态不改变。
4. 不建议因当前日志改阈值、速度估计、屏幕适配或 GPU 后端。上述 wake 修复有独立源码依据，但“用户感知已彻底消失”仍须后续验证。

### 可确定触发的场景与生产路径测试

以下是 **源码可确定构造的触发条件/验证方案，不是本轮已运行复现**。

| 场景 | 生产路径 | 修复前确定现象 / 修复后应有结果 |
| --- | --- | --- |
| CanvasPanMode，valid/contact 不变，连续 Mouse cursor 位置更新 | 真实 WindowController + 绑定 ContactInputCoordinator | 修复前每次 Publish 都增加 wake generation，即使 marker 已 pending；修复后最新坐标仍可读，位置-only 更新不请求 cursor render/control wake。 |
| 同场景中穿插 valid/contact 边沿、ClearMouseCursorSample | 相同生产 mailbox API | 生命周期边沿仍 wake；系统光标刷新原条件不丢；Clear 不留下旧样本。 |
| 绘制模式 Mouse 自绘光标/荧光笔 hover | 相同 Publish API 与真实 resolver | 非 pan 模式仍发 render 请求；不能用全局 Mouse Move 静默规则修复。 |
| 有内容、tile recovery 捕获 generation 后插入 Mouse position sample | 生产 WindowController 发布，再读真实 coordinator 的 HasPendingWork/generation | 修复前满足 Run 的 break 条件；修复后位置-only 样本不增加抢占原因。不要复制被测 predicate 写一个只证明自身的模型。 |
| 活动导航等待前/等待中插入 Mouse position sample | 真实 coordinator WaitForFrameDeadline | 修复前 frameWakeGeneration 使等待提前返回；修复后位置-only 不触发，真实 Down/Up/Command 仍立即打断。优先用受控时序和真实 generation 断言，少依赖易抖动 wall-time。 |
| 有有效 trusted snapshot，视口小幅移动且 tile 未完成 | 真实 renderer 的 offscreen WARP/既有无窗口 GPU fixture | 核对图像中的墨迹平移与 currentViewport 一致；velocity=0 仍平移；不把纯 CanvasPan 模型测试代替实际 fallback pixels。 |
| 无 snapshot / snapshot 无交集 | 同一 fallback renderer 调用 | 明确返回 false 且当前 L2 被保留；用其区分“内容缺失”与“旧坐标延迟”，不扩大本轮实现范围。 |

- 已有入口：`IdtMain.cpp:2035-2036` 的 `--draw3-canvas-pan-feel-test`；`2038-2039` 的 control fence probe。`DrawingController.cpp:7908-7962` 已用真实 coordinator、window、DrainIngressBatch 核对旧 contact / Command / 新 Down 顺序，可复用其无 HWND 测试模式。
- `DrawingController.cpp:170-184` 的 DrainIngressBatch 持续消费实际 ingress，只有带业务 CanvasCommand 的 null marker 置 commandBoundaryPending 后 break；普通光标 control marker 不设置业务 fence。不要以“所有 control marker 都是业务命令”构造测试。
- 对修复机制的无窗口测试足够证明不再出现无效 wake；不能单凭该测试声称实际显示延迟已经修复。

### 后续最小诊断字段

若用户仍感到滞后，优先扩展现有 gated CanvasPan/metrics，而不是永久逐 tile 输出：

- 帧 serial、导航最新 ordinal/sample-qpc、应用 viewport、Freeze publish-qpc。
- recovery 预算/计划进度、完成 tile 数、pending 与 generation 抢占原因。
- trusted snapshot 是否有效、身份/viewport、是否有交集、fallback composite bool。
- 合成开始/结束 QPC、Present 开始/返回 QPC、实际 mode/output target/result。
- 帧预算与实际等待原因（deadline / frame wake / work 已超预算）。

这些字段能定位 callback→consume 之后的 CPU/GPU提交阶段，但 Present 返回也不等于 DWM 已扫描显示；最终视觉跟手仍需用户允许的 GUI/硬件验收。

## Related specs

- `.trellis/workflow.md`：先研究、持久化证据、遵循阶段流程。
- `.trellis/spec/native-desktop/index.md`：区分直接确认、推断和未验证；渲染/输入先明晰线程所有权。
- `.trellis/spec/native-desktop/canvas-pan.md`：唯一动力学真值、直接几何平移、snapshot/tile/backbuffer 各层边界、独立诊断。
- `.trellis/spec/native-desktop/draw3-cursors.md`：选择/穿透清应用主光标；mailbox、系统光标归属与 lifecycle 不得因优化破坏。
- `.trellis/spec/native-desktop/canvas-freeze-boundaries.md`：普通活动 Move 合并、Down/Up/control 可打断等待；lossy 日志不是完整事件链；禁止以过去 freeze 根因推断当前现场。

## External references

- 无。本研究只依据本地生产代码、项目 spec 和用户日志；没有引入外部 API 版本结论。

## Caveats / Not Found

- 本轮未执行编译、probe、GUI、GPU trace 或真实鼠标 A/B，所有“确定”仅指代码分支可确定触发。
- 未发现 MouseContact 令活动 pan 走 idle `continue` 的路径；已发现 Mouse cursor wake 的恢复抢占与节流中断。
- 用户 `delay-ms` 以 callback-arrival 为 sample 时间，仅覆盖到消费，不测硬件→回调，也不测消费→屏幕。多数较小只能收窄排查，不能证明后半链路哪一层超时。
- 有效 snapshot 的几何重投影用当前 viewport，所以不能把 tile 饥饿单独当作所有视觉位置延迟的完整根因。
- 不修改本任务已有 scale-stage 文档；不涉及屏幕尺寸适配或旧动力学调参。

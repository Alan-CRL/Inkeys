# 第2步-A 技术设计：DComp waitable 交换链帧延迟控制

> 依据：父任务 `research/presentation-pipeline-current.md` §7–§8；本任务 `research/codex-investigation-20261117.md`（Codex 只读调查，Claude 已核对关键事实）。行号以 `refactoring/draw@7047db94` 为准。
> 简称 C=`Draw3.DrawingController.cpp`、P=`Draw3.TransparentPresentation.cpp`、H=`Draw3.Host.cpp`。

## 1. 已确认的用户决策（2026-11-17）

1. **超时仍 Present**：waitable 只表示“最多等一个超时”；等到就继续，超时也照常合成并 `Present1`。最坏退化为现状，不影响启动首帧、工作区交接 ready、脏区语义。
2. **预设帧率高于刷新率**：先实现并加开关实测（60Hz / 120Hz 屏），再决定默认策略；超时取值需避免在高帧率预设下误熔断。
3. **开关放设置界面**（性能页“渲染与兼容”，与 DirectComposition 等同组，重启生效）。
4. **诊断只写日志 + 运行指标**，日志使用低于默认阈值的 `Debug` 级别，正常运行不输出。

## 2. 目标行为

| 条件 | 行为 |
| --- | --- |
| 开关开 + Win8.1+ waitable 创建成功 + DComp + 输出目标为主画布 | 帧开头等待句柄（有界），再消费输入、绘制、`Present1` |
| 开关开，waitable 创建失败（Win8.0 等） | 不带 waitable 标志的 FLIP + 设备级 `SetMaximumFrameLatency(1)`（现状） |
| 开关关 | 保留 waitable 交换链但不等待（即现状行为，用于 A/B 与排障） |
| 已熔断（本会话） | 重建为不带 waitable 标志的 FLIP，之后不再尝试；排队仍由设备级上限 1 在 `Present` 内部限制 |
| ULW 模式、辅助输出 SelectionUlw | 不等待（ULW 不 Present 交换链；本任务不改 ULW 的交换链创建，留给第2步-B） |

> 开关按用户命名为“waitable 模式下启用等待”：关闭时回到现状（带标志、不等待），开关只控制“是否等待”，不改变交换链类型。熔断则去掉标志，确保排队仍有设备级上限兜底。

## 3. 组件设计

### 3.1 `FrameLatencyGate`（新增，纯头文件 `Draw3.FrameLatencyGate.h`）

不依赖 D3D/DXGI 的小状态机，便于无窗口单测。等待函数以回调注入（生产为 `WaitForSingleObjectEx(handle, ms, FALSE)`，测试为假实现）。

状态：
- `enabled`：本交换链 waitable 可用且未熔断。
- `tokenHeld`：已等到一次信号、尚未被成功 `Present1` 消耗。
- `consecutiveTimeouts`、`lastAcquireQpc`、`signalIntervalEmaMs`（成功获取间隔的指数平均，用于自适应超时）、`tripped`（熔断，跨交换链重建保留，仅新会话复位）。
- 统计：本帧等待耗时、累计超时、累计成功获取、熔断原因。

接口（示意）：
- `Acquire(timeoutMs, nowQpc, countsTowardTrip) -> {waitedMs, result: AlreadyHeld|Signaled|TimedOut|Disabled}`：未持有令牌时才等；`Signaled` 后置 `tokenHeld=true`、清零连续超时；`TimedOut` 时连续超时 +1（仅 `countsTowardTrip` 为真时参与熔断判定）。
- `OnPresentCalled(bool succeeded)`：仅当**实际调用了 `Present1` 且成功**时消耗令牌（`tokenHeld=false`）。裁剪后空矩形不调用 `Present1`（P:1157）、`Present1` 失败、`DwmFlush` 失败都不算消耗。
- `ShouldTrip()`：连续超时 ≥ N 且距上次成功获取 ≥ T 时为真。
- `Reset()`：交换链释放/重建时清令牌与连续计数（不清 `tripped`）。

### 3.2 `TransparentPresentationController` 改动（P）

- `CreateSwapChain` / `TryCreateWaitableSwapChain`：新增“是否允许 waitable”输入 = 开关开 且 未熔断；不允许时直接走现有普通 FLIP 分支（描述不变，仅不加标志）。
- waitable 创建成功（P:997 之后）调用 `GetFrameLatencyWaitableObject` 取句柄；取不到视为不可用，回退普通 FLIP。
- 句柄与 `FrameLatencyGate` 归 `Impl` 所有；`ReleaseAttempt` / 失败回滚在 `swapChain.Reset()` 之前 `CloseHandle` 并 `gate.Reset()`；Resize 不重建交换链、句柄不变（`ResizeBuffers` 已保留 Flags，R:595）。
- 新增 `PrepareFrameLatency(timeoutMs, countsTowardTrip)`：供帧循环在帧开头调用；仅在“DComp + waitable 启用 + 输出目标为主画布”时执行等待，其余情况立即返回。
- `PresentSwapChain`：若尚未持有令牌（帧循环外的清屏/整帧呈现等），先做一次同样的有界等待（不参与熔断计数），再 `Present1`；根据“是否实际调用 + 结果”调用 `gate.OnPresentCalled`。
- 熔断：`PrepareFrameLatency` 发现 `ShouldTrip()` 时记录 `tripped` 与原因，`SetFailure` 走现有 `RecoverFromRuntimeFailure` 重建路径（同模式、去掉 waitable），复用现有 CPU 重放，不在帧中途直接换链。
- 刷新间隔：交换链创建/恢复时用 `DwmGetCompositionTimingInfo(nullptr, …)` 取 `qpcRefreshPeriod`（dwmapi 已链接，DComp 仅在 Win8.1+ 走此分支，DWM 恒开）；取不到按 16.7 ms。

### 3.3 帧循环接入（C）

- 等待点：`C:21598` 附近——设备恢复与 Resize 处理之后、导航优先 Drain（C:21624）和输入批次（C:22152）之前。理由：此后才消费输入，等待能换来更新鲜的输入；恢复/Resize 可能重建交换链，放在其后避免对旧句柄等待。
- idle 路径 `WaitDequeue`（C:22125）会直接消费命令，第一笔 Down 不严格满足“先等后消费”；但 idle 后队列为空，等待立即返回，不影响结论，不拆分 idle 路径（最小改动）。
- 熔断计数排除：窗口不可见（Host 可见性状态，H:150 / WindowControl:2771、2786）、`RecoveryPending`、DComp 交接屏障进行中、输出目标为 SelectionUlw、工作区交接/加载挂起。这些期间超时只计统计，不参与熔断。
- 等待耗时计入本帧帧预算（保持现有固定预算语义：等得越久，帧末睡得越少），但**不计入“工作耗时”**：画布调试的无限制帧率 / 负载百分比（`CanvasDebug.cpp:754`，以 `workSumMs` 计算）与 `RecordActiveFrame` 的 `workMs` 都应扣除等待耗时；运行指标单独记录 `latencyWaitMs`，不混入 `presentWallMs`。这也是第2步-C 性能看板的口径前提（见 `11-17-perf-dashboard/prd.md`）。

### 3.4 超时与熔断参数（默认值，可在实测后调整）

**核心区分：“GPU 慢”是信号迟到但一定会到；“信号丢失”是根本不到。** 前者必须继续等待（这正是 waitable 要限制堆积的场景），后者才熔断。

- **自适应超时**：`timeoutMs = clamp(1.5 × max(1000 / target_fps, refreshMs, 最近信号间隔EMA), 4, 200)`。
  - 最近信号间隔EMA：令牌成功获取之间的实际间隔（指数平均），反映当前 GPU/合成的真实节奏。性能严重不足、一帧要 80 ms 的设备，超时会自动放宽到约 120 ms，等待照常成功，排队保持一帧，不会因“超时仍 Present”而越排越多。
  - 刷新间隔作下界：60Hz 屏选 120/240 FPS 时，不会因合成节奏本来就慢而频繁超时。
  - 上限 200 ms：只防止异常时长时间卡住；超过它的设备（< 5 帧/秒）本已不可用。
- **熔断**：可计数状态下连续超时 ≥ 8 次 **且** 距上次成功获取 ≥ 1000 ms。只要信号还在来（哪怕很慢），每次成功获取都会把连续超时清零，不会熔断。
- **熔断后仍有限流**：熔断改用不带 waitable 标志的 FLIP，排队由设备级 `SetMaximumFrameLatency(1)` 在 `Present` 内部限制，而不是“不限制”。差别只是 GPU 吃紧时输入会旧一点，不会无限堆积。
- 熔断后本会话不再尝试 waitable；新会话复位。

### 3.5 设置开关与配置

- 配置：`Experimental.Inkeys3.Draw3.EnableWaitableWait`（`Other.Config.cppm` Draw3 组，默认 `true`，`NoUpload`），启动时快照（与 `EnableHardwareAcceleration` 相同，`configOnce`）。
- 传递：`IdtMain.cpp` → `HostStartOptions.enableWaitableWait` → `TransparentPresentationOptions` → `Impl`。
- 设置 UI：`Setting.cpp` 性能页“渲染与兼容”节，DirectComposition 开关之后新增 `##draw3-waitable-wait` ToggleRow，带“重启生效”提示。
- i18n：新增 `SettingsUI.Performance.WaitableWait.N/E`，zh-CN 文案按用户给定：N = “waitable 模式下启用等待”，E = “在 Windows 8.1 及以上系统中，防止帧积压造成的延迟。”；其余语言用 `Scripts/i18n.ps1 sync` 后补译。

### 3.6 诊断（Debug 级别，默认不输出）

- 通过 `DiagnosticOutput`（`Stream::Output`→Debug）或 `Log::TryFormat(Level::Debug, …)`，热路径非阻塞。
- 输出：交换链创建时 waitable 状态（启用/开关关闭/不支持/已熔断）、超时取值与刷新间隔；每 ~1 s 汇总一次（获取次数、超时次数、平均/最大等待耗时）；熔断事件一行（含原因与计数）。不逐帧输出。
- 运行指标：逐帧 `latencyWaitMs` 与等待结果；会话计数 `latencyWaitTimeouts`、`latencyWaitTripped`。
- 设置“画布信息”页已有交换链描述与 waitable 标志显示（Diagnostics:395），本任务不额外扩展；画布调试面板不改。

## 4. 兼容性与不变量

- **“不起反作用”的总原则**：waitable 只在可能堆积时生效。正常情况下句柄立即可用，几乎零等待；堆积时把排队压回一帧，降低延迟；信号迟到时自适应放宽超时继续等；信号真正丢失时最多多等若干超时，随后熔断回设备级上限（仍限流）。任何情况下都不会比“完全没有限制”更差，也不会卡住。
- **Win8.0 / 未来 Win7 DWM 方案（不带 waitable 标志的 FLIP）**：不新增任何应用侧等待，继续依靠设备级 `SetMaximumFrameLatency(1)` 在 `Present` 内部限流（系统内建，无丢信号问题）。仅在 Debug 运行指标中记录 `Present` 耗时，用于发现堆积（`Present` 接近一帧阻塞即说明队列已满），不改变行为。
- Win7 / ULW：交换链创建逻辑不变（仍按现状尝试 waitable 再普通 FLIP；ULW 不 Present，不等待）。Win7 无 `IDXGISwapChain2`，现有回退路径不变。新 API `GetFrameLatencyWaitableObject` 经已有的 `IDXGISwapChain2` 查询获得，`DwmGetCompositionTimingInfo` 为 Vista+ 导出，不引入新的静态导入风险（实现时以 dumpbin 核对导入表）。
- 不引入 GPU 事件查询、自旋或信号量兼容方案。
- 画布调试的输入→上屏延迟仍以 Present 返回为终点；帧循环结构、脏区、合成顺序不变。
- 启动首帧（H:2338 `ClearCanvas` 在 `Run` 前）：初始句柄为已发信号状态，首次等待立即返回；即使超时也照常 Present，不会被判启动失败。

## 5. 回滚

- 用户侧：设置关闭开关（重启生效）即回到“不带标志的 FLIP + 设备级上限”。
- 代码侧：改动集中在新头文件、P 的交换链创建/Present、C 的一处调用、配置/设置/i18n；可整体回退。

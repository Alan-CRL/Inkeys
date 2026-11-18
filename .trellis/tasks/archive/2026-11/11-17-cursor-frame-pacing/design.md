# 第2步-A2 技术设计：绘制时光标跟随画布帧率 + 压力工具

> 代码基准 `refactoring/draw@0c8a7337`。简称 C=`Draw3.DrawingController.cpp`、W=`Draw3.WindowControl.cpp`、CI=`Draw3.ContactInput.cpp`、P=`Draw3.PenCursor.cpp`。行号开工时需重新核对。
> 分工：**Claude 编写全部实现**；Codex（gpt-6.1-sol / high / fast，只读）在完成后审阅；收尾顺序 Claude 独立审阅 → Codex 审阅 → 修复 → 复验。不并行写入。

## 1. 现状（已读码，详见父任务 `research/hover-cursor-layer-20261117.md` §1）

| 路径 | 现状 |
| --- | --- |
| 唤醒 | `RequestDrawingCursorRender()`（W:1901）= 置位合并标志 + `RequestControlWake()` → `PublishControlWake()`（CI:381）入队控制标记（`controlWakePending` 去重）+ `SignalWake()`（CI:468，默认 `interruptFrame=true`，递增 `frameWakeGeneration`）→ 帧尾 `WaitForFrameDeadline`（CI:1325）提前返回 |
| 鼠标光标 | `PublishMouseCursorSample`（W:1836）：信箱 `Publish` 去重（P:248–257），位置变化即 `if (!CanvasPanMode() \|\| stateChanged) RequestDrawingCursorRender()` → 按下书写时逐移动打断帧节拍。两条消息分支（RTS 模式主分支 W:2825→3005；Win32Mouse 兼容 `HandleCompatibilityMouse` W:993→1036）都汇合于此 |
| 笔光标 | `PublishPenCursorSampleImpl`（W:1725 起）：`if (stateChanged \|\| (!inContact && !CanvasPanMode())) RequestDrawingCursorRender()`（W:1801）→ 接触中完全不唤醒，靠活动帧读信箱 |
| 鼠标/笔/触摸笔迹 | Move 走 `PublishMove`（CI:834），单槽快照，不唤醒；每帧 `consumeLatestSnapshot`（C:17907）读一次 |
| 空闲 | 无活动笔画且无请求时 `WaitDequeue`（CI:1149）只在**队列出现条目**时返回；仅递增 `wakeGeneration` + `SetEvent` 不会让它返回 |
| 帧尾节奏 | 只在有物理接触（C:23535）、漫游/动画（C:23558）、活动笔画 deadline（C:23573）时等待；只有光标变化的帧不等待 |
| 拒收触摸 | 关闭多指：`RejectIndependentTouch` → `DiscardUntilTerminal`（C:5383–5405，调用 C:17442）；准入阻塞：`!ContactAdmitted` → 丢弃（C:18318）；手掌邻近：`RejectPalmNeighborTouch`（C:17436）。被拒收的触摸没有运行时对象，触摸光标（C:18760–18810，只从活动笔画生成）不显示 |

## 2. 唤醒模型：新增“不打断帧节拍”的唤醒

- `ContactInputCoordinatorImpl::PublishControlWake(bool interruptFrame = true)`：逻辑与现有相同（入队控制标记、`controlWakePending` 去重、失败兜底），只是最后调用 `SignalWake(interruptFrame)`。对外新增 `ContactInputCoordinator::PublishPacedWake()`（= `PublishControlWake(false)`）。
- 语义：唤醒空闲等待（`WaitDequeue` 靠队列标记、`WaitForWake` 靠 `wakeGeneration`），但**不**让进行中的 `WaitForFrameDeadline` 提前返回。
- 与现有打断式唤醒混合：若打断式唤醒到来时已有待处理标记，`PublishControlWake` 走“只 Signal”分支，照常打断，不丢事件。
- 开工核对项：活动帧里每帧最多会多处理一个控制标记（去重保证），需确认 `processCommandAndReconcile(nullptr)` / drain 处理空标记没有可观开销或副作用（如 `commandBoundaryPending`）。若有，改为专用标记类型。

## 3. 窗口侧

新增 `WindowController::RequestDrawingCursorRenderPaced() noexcept`：置位同一个合并标志 + `PublishPacedWake()`（不操作 HWND/GPU，符合 `draw3-cursors.md` 对 `RequestDrawingCursorRender` 的约束）。

| 函数 | 改后条件 |
| --- | --- |
| `PublishMouseCursorSample` | `stateChanged` → 打断式；未按下且非移动模式（悬停）→ 打断式（不变）；**按下且非移动模式** → 若新选项打开则打断式（现状），否则 `RequestDrawingCursorRenderPaced()` |
| `PublishPenCursorSampleImpl` | `stateChanged` 或悬停 → 打断式（不变）；**按下且非移动模式** → `RequestDrawingCursorRenderPaced()`（新增；新选项只管鼠标，不影响笔） |

- 新选项值存 `std::atomic<bool> unthrottledMouseMoveWhileDrawing_`，窗口线程读取，即时生效。
- 正常笔/鼠标书写时：活动帧本来就在按帧率运行，置位标志只让本帧把光标旧/新区域并入脏区（现有逻辑），帧节奏不变。

## 4. 帧循环（控制器）

### 4.1 光标接触节流分支

- 帧尾新增分支（位于“物理接触”与“漫游/动画”分支之后、`!active.empty()` 之前，开工时按实际结构定位）：条件 = 本帧之后**没有**物理接触笔画，但存在“处于接触中的光标”：
  - 鼠标信箱 `valid && inContact`、笔信箱 `valid && inContact`（不被触摸平移/抑制置无效），且非选择模式、非移动模式；或 §4.2 的拒收触摸光标非空。
- 动作：`WaitForFrameDeadline(1000/target_fps − workMs)`。由于接触中的移动只发不打断唤醒，等待会持续到帧截止；落笔/抬笔（状态变化）仍打断。
- 回到循环开头后：标志已置位 → 渲染一帧光标；未置位（光标没动）→ 进入空闲等待，直到下一次节流唤醒。效果：**每个帧间隔最多一帧光标**，按住不动不空转出帧。
- 拒收的鼠标/笔接触：信箱照常更新（鼠标 `inContact = 按键按下`；笔来自 RTS `Packets`），无活动笔画，走本分支 → 光标按帧率跟随、不出墨。
- 开工核对项：拒收接触时 `buildDrawingCursorVisuals` 是否仍生成主光标（主光标归属、`mouseSpeedEraser().LastEventSeconds()` 把 `inContact` 置否的条件，C:18620–18660）；橡皮 F3 idle 交接（`WaitForEraserIdle` C:5859，`input-and-ink.md` F3 合同）在接触中不再收到打断式唤醒后是否仍正确——它依赖 `wakeGeneration` 订阅还是 `frameWakeGeneration` 需逐行确认。

### 4.2 拒收触摸光标（范围 ①②）

- 在 ① 准入阻塞（C:18318）与 ② 关闭多指（`RejectIndependentTouch`）两处丢弃时，若该触摸按当前工具会有光标（触摸软/硬笔且 `cursorSettings.touch != None`，或橡皮），登记 `rejectedTouchCursors`：`{ handle, tool, 外观快照（Down 时锁存） }`。
- 每帧对登记项 `TryReadSnapshot` 取最新位置生成光标（复用 `MakeTouchPenDrawingCursorVisual` / `MakeTouchEraserDrawingCursorVisual`）；读到终态或句柄失效即移除。
- ③ 手掌邻近、④ 平移抑制、⑤ 选择模式、⑥ 兼容消息抑制、激光多指限制：保持现状，不登记。
- 开工核对项（**先验证再实现**）：`DiscardUntilTerminal` 之后 producer 的 `PublishMove` 是否仍写入该记录、`TryReadSnapshot` 是否仍可读、记录复用（generation）如何识别。若不可读，改为在 `ContactInputCoordinator` 为“丢弃中的触摸”提供只读位置接口；若需要的改动超出本设计，停止并向用户报告。
- 资源释放：工作区切换、Resize、设备恢复、选择模式切换、Host 停止时清空登记。

## 5. 设置项

| 项目 | 内容 |
| --- | --- |
| 配置键 | `Experimental.Inkeys3.Draw3.UnthrottledMouseMoveWhileDrawing`，`IdtAtomic<bool>`，默认 `false`（紧挨 `EnableWaitableWait`，`Other.Config.cppm:579` 附近） |
| 下发 | 优先复用已即时生效的光标设置链路：`CursorSettings` 增字段（`Draw3.CursorSettings.h`，打包位 13）→ `IdtState.cpp:674` 附近从配置读取 → `Draw3.Host.cpp:1840` `SetCursorSettings` → `DrawingController::SetCursorSettings`（C:15142）→ `window_.SetUnthrottledMouseMoveWhileDrawing()`。开工时确认设置页修改后状态会重新下发；不能即时生效则改为重启生效并在汇报中说明 |
| 界面 | 性能页“渲染与兼容”，`WaitableWait` 之后，`ToggleRow`；i18n 键 `SettingsUI.Performance.UnthrottledMouseMove.{N,E}` |
| zh-CN 文案 | N：绘制时鼠标移动不受画布帧率限制<br>E：开启后，按住鼠标绘制时每次移动都会立即刷新画布，鼠标笔迹采样更密，但会明显增加 CPU 和 GPU 负载。关闭时按画布帧率刷新。 |
| 其他语言 | `pwsh 7` 运行 `Scripts/i18n.ps1 sync` 后由 Claude 补译 19 种语言，`i18n.ps1 check` 通过 |

## 6. 测试（InkeysHeadlessTests / 现有 Draw3 测试 rig）

新增：

1. `PublishPacedWake` 唤醒 `WaitDequeue` / `WaitForWake`，但不让 `WaitForFrameDeadline` 提前返回；打断式唤醒与节流唤醒混合时不丢事件。
2. 鼠标按下期间纯移动：发节流唤醒、不递增帧唤醒代次；按下/抬起仍打断；新选项打开时恢复打断。
3. 笔接触中纯移动：发节流唤醒；悬停、状态变化行为不变。
4. 无活动笔画 + 光标接触（模拟拒收）：帧循环按帧间隔出光标帧，光标位置跟随，无墨迹提交；按住不动不出帧。
5. 拒收触摸（准入阻塞、关闭多指）光标出现、跟随、抬起后消失；手掌邻近拒收不出光标。

更新：C:9223–9346 一带检查 `ConsumeDrawingCursorRenderRequest` 与 `controlWakes` 的既有测试，以及鼠标兼容过滤、橡皮 F3 idle 相关测试，按新预期调整（只改预期，不改产品意图）。

## 7. 压力工具

### 7.1 文件与构建

| 文件 | 说明 |
| --- | --- |
| `Scripts/StressLoad/StressLoad.cpp` | 单文件 Win32 + D3D11 程序，UTF-8 BOM + CRLF |
| `Scripts/Start-StressLoad.ps1` | 用 `vswhere` 找本机 VS → 导入对应架构开发环境 → `cl /std:c++20 /O2 /EHsc /MT /utf-8` 编译到 `Build/Tools/StressLoad/<arch>/StressLoad.exe`（`/Build/` 已在 .gitignore）→ 启动。参数 `-Arch arm64\|x64`（默认 arm64）、`-BuildOnly`、`-NoBuild`，其余参数透传给程序 |
| `Scripts/Start-StressLoad.cmd` | 双击入口，调用 ps1（与现有 `Start-*Diagnostic.cmd` 一致） |

不加入 `InkeysRepo.sln`、不进 CI、不进自动测试。依赖仅 `d3d11.lib` / `d3dcompiler.lib` / `user32.lib` / `gdi32.lib`，运行时只需系统 DLL，可拷到其他测试机运行。

### 7.2 行为

- 打开即开始施压；关闭窗口 → 置停止标志、join 全部线程、释放设备后退出。
- **CPU**：N 个忙线程做浮点运算（不 Sleep），默认 = 逻辑处理器数的一半，可调 0…逻辑处理器数。
- **GPU**：独立渲染线程；硬件 D3D11 设备（默认适配器，显示名称）；离屏渲染目标默认 = 主显示器分辨率（BGRA8）；每帧绘制 `L` 次全屏三角形，像素着色器做固定次数的三角/乘加循环；`L` 可调 1…64，默认 8；用事件查询把在途帧限制为 2，避免命令无限堆积。
- **TDR 保护**：单次绘制工作量固定且较小，强度只通过“每帧绘制次数”调节；测得单帧 GPU 耗时超过 500 ms 时自动降一档；设备移除时停止 GPU 部分并在窗口显示原因，CPU 部分继续。
- **窗口**：约 720×360，GDI 文本每 250 ms 刷新：CPU 线程数/上限、GPU 档位、每帧绘制数、渲染目标尺寸、GPU 帧耗时与帧率、适配器名、暂停状态、按键说明。
- **按键**：`↑/↓` GPU 档位 ±1，`←/→` CPU 线程 ±1，`G` 开关 GPU，`C` 开关 CPU，`Space` 全部暂停/继续。命令行：`--cpu N --gpu L --rt WxH`。
- 不做熔断/故障注入，不读写 Inkeys 配置或日志。

## 8. 兼容性与不变量

- 只改“按下期间纯位置变化”的唤醒方式；按下/抬起/有效性/悬停/移动模式/选择模式的唤醒不变。
- 阶段 A 的 waitable 等待、帧延迟句柄、熔断逻辑不变。
- 热路径不新增日志、锁或分配；拒收触摸登记表容量受活动触摸上限约束。
- 改动的 `.cpp/.cppm/.h` 保持 UTF-8 BOM + CRLF。

## 9. 回滚

- 新选项打开即回到鼠标的现状行为（笔接触中的节流唤醒与拒收光标不受选项影响）。
- §2–§4.1、§4.2、§5、§7 各自独立，可分别回退；§4.2 依赖 §4.1 的节流分支。

## 10. 开工核对结论（2026-11-17，Codex 只读核对 + Claude 抽查，详见 `research/startup-verification-20261117.md`）

已完成并编译通过（Debug|ARM64）：§2 `PublishPacedWake`；§3 `RequestDrawingCursorRenderPaced`、鼠标/笔请求条件、`unthrottledMouseMoveWhileDrawing_`；§7 压力工具（ARM64/x64 编译、短时运行与关闭退出已验证）。

与设计一致：控制标记处理为稳定态记账（有锁与状态快照，无墨迹提交/GPU 工作），鼠标原本就走同一路径；橡皮 F3 idle 等待依据 `wakeGeneration`，节拍唤醒照常唤醒它（新帧尾分支必须要求“无物理接触笔画”，否则会绕过 F3）。

实现细节修正（不需用户决策）：
- `CursorSettings` bit13 是有效位，新字段改用 **bit 44**。
- waitable 开关只写配置；新开关必须显式调用 `SyncDraw3State()` 才能即时下发。
- 帧尾分支读取笔样本时须沿用有效抑制（`ReadPenCursorSample(..., &suppressed)` / 触摸平移），保持④现状。
- 既有测试（平移、悬停、F3、隔离）基本不改，只新增接触中节拍唤醒与拒收光标测试。

与设计矛盾、**待用户决策**：
- D1 拒收触摸：`DiscardUntilTerminal` 后记录转为 Quarantined，`PublishMove` 不再写入、`TryReadSnapshot` 不可读，没有现成接口。需在 `ContactInputCoordinator` 新增“被观察的丢弃接触”：拒收时选择性登记，producer 继续更新位置但不进墨迹，按代次读取（存活/重试/结束或复用），位置变化发节拍唤醒。
- D2 兼容鼠标准入拒收：`MouseInputState::Down` 拒收后 `Active()=false`，光标样本 `inContact=false` → 被当作悬停（按事件唤醒、悬停外观）。可让样本 `inContact = Active() || 准入拒收按键仍按住`。
- D3 手掌拒收的笔/鼠标：现状并非隐藏，光标保持显示；手掌接触期间帧循环本就按帧率运行，手掌抬起后笔仍被拒收时光标停住。若只按信箱判断，新分支会让它跟随；保持“停住”需额外记录拒收原因。

**用户决策（2026-11-17，“按建议继续”）**：D1 采用观察通道（`ObserveDiscardedContactCursor` / `TryReadObservedContact`，记录带代次的观察标记，被观察的 Quarantined Move 只更新位置、仍返回 false、发节拍唤醒）；D2 兼容鼠标新增 `CursorHeld()`（准入拒收且仍按住视为光标接触，选择隔离不算）；D3 手掌拒收的笔/鼠标光标同样跟随，不记录拒收原因。帧尾分支条件为 `active.empty() && cursorContactPacingActive()`。

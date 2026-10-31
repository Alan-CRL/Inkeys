# 第三轮：其他窗口关联的深入调查

日期：2026-10-15。基线：`5d319bbf5c520dec97dcb37ad2fcc7e26d029bbb`。完整产品使用此前构建成功的 Debug ARM64，exe SHA256 `9316b86f5d08f0135b8e583fe490211fbcfb59f1c5c0fa055a3abea8865347fd`。仅调查，任务保持 planning，没有产品源码或行为修复。

## 结论

**新增一个有完整产品触发/恢复证据的窗口输入缺陷 F4：隐藏的 Settings 可以残留鼠标捕获，使主栏工具点击持续失效。** 当前强触发是 Settings 内按住鼠标时用 Alt+F4 关闭，再正常抬起。无需诊断输出，也无需人为阻塞线程。

同一故障现场仍能用鼠标画出新线，因此 F4 是工具交互失效，不能称为已经确认的 Draw3/RTS 卡死，也不能据此承诺解释所有“UI 能操作、笔和橡皮都失效”的用户现场。普通关闭按钮完整点击与这个“按下期间隐藏”的时序不同。

本轮没有确认第四条独立的画布永久等待/死锁。进一步收窄了显示上下文回调进入窗口同步等待的入口，但缺少反向等待证据。对于原始报告，根因仍未确认；F1/F2/F3 原结论不变，参见 [前轮报告](expanded-investigation.md)。

## F4：完整产品的强触发与恢复对照

运行副本为 `Build/CanvasFreezeInvestigation/run-window/`。四项 ConsoleOutput 全为 false，移除 `INKEYS_DRAW3_WIN7_DIAGNOSTICS`，stdout 指向 DEVNULL、stderr 普通文件，无交互控制台、无停读管道。PID 29472；主 Drawpad `0x20e18` / TID 11640；Setting `0x50e06` / TID 14644；Bar `0xa0d94`。

1. 进入软笔并画线，确认正常绘制。
2. 通过 More → Settings 打开设置，在内容区按下鼠标左键。
3. 保持按下，实际发送 Alt+F4，Settings 被隐藏。
4. 正常抬起鼠标，再在画布画一笔，再点击主栏橡皮。
5. 等待和继续点击未使 capture 自解；仅向 Settings HWND Post `WM_CANCELMODE`，再点击同一橡皮位置。

| 观测点 | Settings 可见 | GetGUIThreadInfo 中的 capture | 实际效果 |
| --- | --- | --- | --- |
| Settings 内容区按下 | 是 | `0x50e06` | 设置获得捕获 |
| Alt+F4 隐藏，尚未抬起 | 否 | `0x50e06` | focus 已转主 Drawpad |
| 正常抬起后 | 否 | `0x50e06` | 隐藏设置仍捕获 |
| 再画一笔、点击橡皮 | 否 | `0x50e06` | 新红线出现；主栏橡皮未切换，残留持续数分钟 |
| 仅 Post WM_CANCELMODE 后 | 否 | `0` | 再点击此前相同橡皮位置，正常切换并高亮 |

证据目录 `Build/CanvasFreezeInvestigation/window-interaction/`：

- `capture-hide.json` / `capture-hide.png`：按下、隐藏、正常抬起、新线后的真实窗口/线程状态和屏幕结果。
- `capture-toolbar.png` / `capture-toolbar-windows.json`：故障中橡皮点击无效。
- `capture-recovery.json` / `capture-cancel-recovery.png`：同进程取消前、取消后、同位置再点击的对照。

截图证明新墨迹和点击效果；JSON 证明 capture/owner/focus。不能仅凭一个非空 capture 或旧按钮高亮推导 Draw3 停顿。

### 原码链

- `Inkeys/additional/imgui/imgui_impl_win32.cpp:698–705`：Down 时 SetCapture，并记录 MouseButtonsDown。
- `Inkeys/Inkeys/UI/Setting/Setting.cpp:647–658`：SC_CLOSE/WM_CLOSE 调用 Hide。
- `Setting.cpp:8860–8869`：先将逻辑 visible 设为 false，再排入原生隐藏命令。
- `Setting.cpp:610–617`：只有逻辑 visible 为 true 才让消息进入 ImGui backend，因此晚到的 Up 跳过 backend。
- `imgui_impl_win32.cpp:719–721`：原本由最后一个按钮 Up 释放 capture，此路径被跳过。
- `Inkeys/Inkeys/Window/Window.cpp:1733–1741`：Hide 命令直接 ShowWindow(SW_HIDE)，不主动收束该 Settings 的捕获。

标准自绘关闭按钮通常在 Up 处理后才触发 Hide（`Setting.cpp:1433–1436`），本轮没有证明完整点击 X 必现，也没有把触摸、多输入源关闭或任务栏关闭自动列为已证触发。

### 跨线程 owner 的受控对照

独立 `window_owner_capture_probe.cpp` 使用真实 USER32、生产 ImGui Win32 backend 和生产 `Window.CommandCompletion.h`。两个真实 HWND 分属两线程；相同 Down → Hide → Up，唯一区别是 Setting 有无跨线程 owner。

| 对照条件 | Hide/Up 后实际结果 |
| --- | --- |
| owner=null | 系统触发取消，capture 自动清零 |
| owner=另一线程 root | focus 转 root，capture 仍指向隐藏 Setting；正常 Up 后仍不释放 |
| owned 情况下发送 WM_CANCELMODE | 两个关联线程查询到 capture 均清零 |

编译/运行均退出 0；`residual_hidden_capture_cases=1` 是观察到缺陷，不是“所有测试无缺陷”。另有 200 次 owner 变更与 200 次根窗口 TOPMOST 切换自然交错，全部完成，无失败、无本次死锁。模型未包含 Draw3/RTS 或完整 Setting coroutine，不能替代完整产品复现。

原始结果：`Build/CanvasFreezeInvestigation/window-probes/capture-run.log`、`initial-run-results.json`。详见 [owner 与同步等待审计](window-owner-deep-audit.md)。

### 修复建议（尚未实施）

在 Settings HWND 所属线程的隐藏/会话结束边界，收束仍属于该窗口的捕获和 backend 按钮状态。检查 capture 的真实所有者，避免释放已转给 Bar/Drawpad 的新捕获；不要在任意 render/worker 线程直接 ReleaseCapture。单纯移除 visible guard 会扩大 ImGui 生命周期访问，不能当作未经审查的快捷修复。

回归至少包括：按住时 Alt+F4 → 正常 Up → 工具点击；标准 X 关闭；关闭后重开并点击；隐藏时 capture 已归其他窗口；鼠标及真实触摸/笔混用。验收分别观察工具业务状态和真实新笔迹。

## 完整产品的正常窗口流程

同一 PID 29472、诊断全关闭，用实际鼠标完成三轮“软笔 → 设置 → 实际 X 关闭 → Inkeys 文件管理器入口 → 选择 → 向识别出的本轮新 Explorer HWND 发送 WM_CLOSE → 软笔 → 新线”。三轮均出现新红线；工具往返时主/辅助表面可见性正确交换，capture 为 0。

此外实测 Win+D 往返后新线成功；真实按住绘制时 Win+E 打开 Explorer、正常 Up、工具往返后新线成功。Win+D 时主 Drawpad 实际仍可见，不能把它写成已经验证了主 HWND 被系统隐藏后的恢复。

还发送过 Setting 最小化/还原命令，关闭后新线成功，但这组完整产品采样没有记录 IsIconic，因此不声称成功覆盖了实际最小化状态。下面的独立 RTS 探针则记录了 IsIconic 正反变化。

证据：`window-interaction/normal-sequences.json`、`normal-sequence-0/1/2.png`、`external-boundaries.json`、`win-d-new-stroke.png`、`held-explorer-new-stroke.png`、`setting-minimize.json`、`setting-minimize-new-stroke.png`。这只表示本机这些次数未复现，不排除用户机器的时序、驱动或版本差异。

## 真实 OS + RTS 窗口边界矩阵

`rts-window-boundary-probe.cpp` 创建真实 NOACTIVATE + LAYERED + TOPMOST 窗口，使用真实 IRealTimeStylus、MTA 同步插件、AllTablets/MultiTouch。输入通过系统鼠标或 InjectTouchInput 产生，不直接调用 StylusDown/Up。回调只写预分配内存，结束后输出 CSV。

每组包含：正常基线一笔；按住时发生边界并始终正常 Up；恢复窗口但不强制前台的新一笔；再显式前台恢复后新一笔。

| 动作 | 鼠标 | 注入触摸 | 实际状态核对 |
| --- | --- | --- | --- |
| none 基线 | 4 Down / 4 Up | 4 Down / 4 Up | 有效输入正对照 |
| hide | 4 / 4 | 4 / 4 | visible 1→0→1 |
| disable | 4 / 4 | 4 / 4 | enabled 1→0→1 |
| minimize | 4 / 4 | 4 / 4 | IsIconic 0→1→0 |
| focus | 4 / 4 | 4 / 4 | 障碍窗实际获得前台/焦点 |
| cover | 4 / 4 | 4 / 4 | 障碍窗实际获前台并改变尺寸；未独立记录完整遮挡命中 |
| capture | 4 / 4 | 4 / 4 | capture 从 canvas 转至 other，有 WM_CAPTURECHANGED |
| cancel | 4 / 4 | 4 / 4 | 自身先 capture 再 WM_CANCELMODE，捕获清零 |
| leave | 4 / 4 | 4 / 4 | 完整接触移出输入区后正常 Up |

18 组均无 RTS Error 或有效试验内注入失败。Down phases 都为 0/1/3/4，Up 为 0/2/3/4。触摸在 phase 0 创建注入设备时出现 TabletAdded → Disabled → Enabled（15–31 ms），早于首个 Down；窗口边界 phase 2 没有 Disabled，末尾 Disabled 来自主动 shutdown。

focus/cover 前 canvas 本来就非前台，所以不能声称覆盖了“已激活画布失焦”；它们验证的是另一前台窗口介入时，后台 NOACTIVATE RTS 能否继续。该矩阵也没有生产 decoder、admission、Controller、renderer、完整 owner 拓扑，不能宣称完整产品触摸已通过。

初版触摸参数 maxCount=1 / pointerId=7 的 Down 返回 87，试验无效；只改独立探针默认 ID=0 后获得 4/4 基线；同一修订版 ID=7 单因素复测仍返回 87。没有把这项探针参数问题算作产品缺陷。

原始证据：`Build/CanvasFreezeInvestigation/window-probes/{action}-mouse.csv`、`{action}-touch-id0.csv`、`mouse-matrix-summary.json`、`touch-matrix-summary.json`。详见 [输入深审](window-input-deep-audit.md)。

## 完整产品触摸补测的限制

独立 RTS 基线成功后，进一步在完整产品 PID 29988（诊断全关闭）使用 `touch_inject_probe.py` 注入同类单触点，26 次 API 调用全部成功。正确切入软笔后没有出现新墨迹；随后同进程鼠标画线成功。这发生在任何设置/Explorer 操作之前，不能作为窗口诱发冻结。

为区分注入结果与真实输入到达，另建 `run-touch-diag/` 隔离副本，只启用已有 Cursor 日志，stdout/stderr 为普通文件。PID 872 中同样触摸无墨迹、鼠标成功；`diag-stdout.log` 记录触摸 WM_POINTER Down/Move/Up 和鼠标 RTS Down/Up arrival/published；seq 1–458 连续无缺口，覆盖本次触摸到鼠标对照，没有触摸 RTS arrival。其后出现 dropped=176（seq 459–634 缺失），因此负证据只限定在此前连续时间窗，不能称整份日志零丢失。产品 DComp HWND 样式及 owner 关系与独立探针不同，尚未确定哪个差异造成该注入表现。这里没有产品热路径诊断改动，没有停读管道，也没有用手工调用回调补齐证据。

因此没有继续把这条未建立完整产品正对照的注入路径跑成“触摸窗口流程通过”。完整产品真实硬件触摸验收仍缺失。最初 `touch-baseline.*` 是工具尚处选择态的准备记录，不计绘制验证；`touch-valid-baseline.*` 表示已选软笔的基线尝试，名称不表示绘制成功。

证据目录 `Build/CanvasFreezeInvestigation/window-touch-product/`：`touch-valid-baseline.json/.png`、`mouse-control.png`、`diag-comparison.json/.png`、`diag-stdout.log`、`cleanup.json`。

## 等待与呈现链：新收窄、尚未证实的入口

1. **显示上下文回调可以让 Draw3 等窗口事务。** `Draw3.Host.cpp:1230–1239` 只有在首次、monitor 身份或 context 变化时调用回调；`IdtMain.cpp:3242–3255` 在 `ApplyPenWidthContext` 返回 false 时进入 `SyncDraw3State()`，可能等 Setting owner 命令的 future。例子是同规格双屏间 monitor 身份变化、业务 context 仍等价。尚无反向等待边，且普通开关设置不必走该分支；本机未做真实多屏迁移。
2. **设置原生 modal loop 可暂时延迟命令队列。** Window Service 在外层 loop Drain 应用命令；系统拖动/菜单循环未必执行该 Drain。但退出 modal 后可以继续，不能把临时等待说成永久死锁。
3. **系统额外隐藏后的可见性复核是条件化边界。** 应用自身显隐失败有重试；如果成功后被系统额外隐藏，并且没有任何新 revision，可能不立即重读 HWND。但本轮没有发现 Explorer/Settings 自然造成这一状态；工具切换会再 reconcile。

已排除的简单推理：选择态切换只改输出目标和全帧请求，不等于 DComp 重建/WaitForCommitCompletion；显隐事务失败有双隐藏保护及重试；ControlWake 在消费前 acknowledge，工具更新会重新 wake。Settings Present 持 ImGui mutex 是待证外部依赖，尚无本次实际 DXGI→WndProc 等待和锁拥有者证据。

详见 [呈现与窗口交接审计](window-render-deep-audit.md)。本轮没有暂停线程、永久占锁、把 API 桩成不返回，以此制造“新死锁”。

## 后续取证与验证边界

原始故障需要同一时刻区分：真实输入未到 RTS、已到未发布、已发布未消费、消费后输出/窗口未生效。下一份诊断应保存轻量阶段序号、最近 RTS Down/Up、writer 阶段、admission/command boundary、绘制心跳和 target revision，再由独立采集者保存线程栈与真实 HWND/capture。现有快照含锁，不能直接放入无锁 watchdog；不能向实时链新增同步控制台 I/O。

F4 的修复可以按上述明确合同独立实施与验收，但它的成功不等于用户画布冻结已解决。其余候选需先有故障现场或真实触发证据，不以批量加超时/重启 RTS 代替定位。

本轮新增两项独立 C++ 探针均 ARM64 编译成功；owner 探针运行 0，RTS 有效矩阵 18 组运行 0。产品源码未改，因此没有重复完整 solution 构建；沿用此前同 HEAD 已通过的构建与 headless 结果。独立 RTS 探针有本地变量遮蔽的 C4459 warning，不影响本轮输入实验，未为消除 warning 修改产品。

已正常关闭隔离 Inkeys PID 29472、29988、872，独立探针自动退出。仅关闭能确认属于本轮的 Explorer HWND；无法确认归属的 Explorer 保留，不结束用户 Explorer 进程。所有运行副本、CSV、截图与编译物均在忽略的 Build/CanvasFreezeInvestigation 下，任务目录只保存研究与可复用探针。

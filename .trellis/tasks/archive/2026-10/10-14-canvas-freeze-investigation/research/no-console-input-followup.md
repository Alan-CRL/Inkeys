# Research: 无控制台反馈与工具、输入交接的定向复核

- Query: 判断 F1/F2/F3 能否解释“启动画几笔 → 打开关闭设置 → Inkeys 启动 Explorer → 选择并关闭 Explorer → 画笔橡皮失效”，且用户明确未开启控制台输出；复核 Select/Pen/Eraser、admission、焦点与 RTS 生命周期，不修复。
- Scope: internal；沿既有研究与具体产品入口的静态复核，无构建、GUI 或新增故障注入。
- Date: 2026-10-15
- Task: `.trellis/tasks/archive/2026-10/10-14-canvas-freeze-investigation`
- 最新事实: 这位具体用户的输入设备未知；全部反馈中多数涉及触摸，也有鼠标。尚未证明同一次故障中同一用户的触摸和鼠标同时失效。

## Findings

### 结论与证据边界

F1/F2/F3 是已经确认其各自条件和行为的缺陷，并不是该操作链的三个已确认根因。新增反馈进一步削弱 F1 的现场匹配度。当前不能承诺修复三者后这批反馈即消失。

| 项目 | 与本次反馈的匹配判断 | 修复信心的边界 |
| --- | --- | --- |
| F1 同步诊断输出阻塞绘制线程 | 若“未开启任何控制台输出”指相关诊断开关关闭，则已验证的高频日志阻塞链缺少触发前提。仅“看不到控制台窗口”与“没有重定向输出”不是同一事实，但不应无证据地假设用户有隐藏管道来维持此归因。 | 对隔离同步 sink 的修复方向有把握；只能承诺对应停读实验通过，不代表普通无诊断场景解决。 |
| F2 PPT 冷槽读取失败被错误归类 | 原操作链没有 PPT 已保存墨迹槽恢复、读取失败或文件共享冲突证据。普通 Desktop 路径不经过该 PPT 加载门。 | 错误分类及重试恢复有直接代码与组件证据，适合定向修复和回归；不能推广到这次桌面操作链。 |
| F3 Touch/Pen 速率橡皮休眠后 Move 不 wake | 触摸反馈比例提高其作为独立缺陷的相关性，但具体用户设备未知，也没有“速率橡皮持续按住、静止后再次移动”的事实。有效 Up/Cancel 或成功发布的工具切换会产生新 wake，所以它不能独立解释后续工具切换、重新落笔仍长期全部失效。普通真实鼠标移动另有 wake。 | 原码实验已验证 Touch 分支与 Up 恢复，修复唤醒协议有明确目标；Pen 的实际产品可达性和完整硬件行为仍需验证。不能用它统包不同报告中的触摸和鼠标故障。 |

“多数触摸、也有鼠标”说明问题范围横跨多个报告中的设备类型；不能改写为“同一现场两种设备一起死”，也不能反过来据此断言所有报告同根因。该区分影响 RTS 全局停投递与单设备 route/唤醒故障的排查优先级。

### 实际按钮与状态发布链

- `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp:277`：Select 调 `ChangeStateModeToSelectionFromButton()`。
- `Inkeys/IdtState.cpp:1104–1120`：仅分离式选择模式（配置为 1）、非白板且有内容时先发布 Clear；Clear Accepted 后才进入选择并 `SyncDraw3State()`。不能把每次 Select 都视为 canvas command。
- `Bar.Button.cpp:314–317` 与 `:376–383`：从另一工具切回 Pen/Eraser 分别进入 `ChangeStateModeToPen()` / `ChangeStateModeToEraser()`。
- `IdtState.cpp:1248–1254`、`:1354–1365`：工具状态和 revision 更新后调用 `SyncDraw3State()`。
- `IdtState.cpp:723–746`：先处理 Setting owner，之后 `PublishDraw3State()`，再协调画布显隐。不能仅凭 Bar 高亮已变就确认后两步完成。
- `IdtState.cpp:215–229`：桥接真实 `tool` 与独立 `selectionMode`，不是将光标 `tool=0` 解释为选择。
- `Inkeys/Inkeys/Business/ComponentActions.cpp:52–54`：Explorer 入口只是 `ShellExecuteW(..., L"explorer.exe", ...)`，不等待其退出，不是 Inkeys 文件选择模态，也不直接关闭 RTS/admission。

### 有效工具切换确实提供 wake；必须区分“点击了”与“发布完成”

1. `Draw3.Host.cpp:2171–2179` 的 `Host::PublishState()` 发布 bridge、刷新 admission 后，在已附着 HWND 时执行 `input.PublishControlWake()`。这是工具发布到消费者之前的唤醒，不能只引用消费者内部的二次 wake。
2. `Draw3.DrawingController.cpp:9827–9830` 消费控制 marker 时调用 `observer.controlWake`；`Draw3.Host.cpp:1416–1424` 的 `ConsumeBridge()` 最终执行 `PumpBridgeState()`。
3. `Draw3.Host.cpp:1254–1276` 消费新 revision，设置 selection 和 active tool。`Draw3.WindowControl.cpp:445–450` 在 selection 实际变化时发送 `RequestControlWake()`；`:431–437` 的 `SetActiveTool()` 也发送。
4. F3 的等待比较 generation，新的真实 wake 能解除该空循环；`Draw3.ContactInput.cpp:638–639` 的有效 Close/Up/Cancel 也发送 wake，详见 `input-permanent-stall-audit.md` 与已执行的输入探针。

因此不能说“F3 导致点任何工具都没用”。但按钮业务如果停在 `ApplySettingOwnerDesiredState()` 之前/内部，或 marker 后续被另一 command boundary 挡住，则状态可能未被完整应用；这属于另一条待证明问题，不能把其前提省略。

重复点击已经选中的 Pen 可能只展开属性，`ChangeStateModeToPenTool()` 的无变化路径还会仅递增用户意图 revision（`IdtState.cpp:1273–1277`）；这种点击不能作为“已完成新的工具状态发布”的证据。真正 Pen ↔ Eraser 往返更有判别力，但仍应观察 bridge/applied revision。

### 选择显隐、焦点与 RTS 生命周期

- `IdtState.cpp:523–543` 等待输出 target/revision/content 就绪后，才在 `:572` 提交 surface visibility；未就绪保留现有表面并要求重试。主窗隐藏/辅助窗显示由已应用的 selection 状态决定。
- `Draw3.DrawingController.cpp:12608–12620` 在 Selection 与 Primary 输出间切换；这是输出目标变化，不是每次销毁并重建 RTS。
- `Window.cpp:1976–2028` 在隐藏主 Drawpad 前检查并释放该线程的主窗 capture，再批量切换两窗显隐。它没有向所有窗口广播取消，也不保证操作系统 RTS 必然投递怎样的终态。
- `Draw3.WindowControl.cpp:1507–1510` 的鼠标激活只返回 `MA_ACTIVATE/MA_NOACTIVATE`；`:1526–1528` 的 `WM_SHOWWINDOW/WM_ACTIVATE` 请求完整呈现。此处没有“失焦后永久暂停输入”的应用字段。
- `Draw3.RealtimeStylus.cpp:2687` 初始化启用 RTS；`:2717` 在 Shutdown 才显式禁用。设置打开/关闭、Explorer 按钮未发现直达 disable/rebind 的入口。
- `Draw3.Host.cpp:558–562` 的 admission 门只由场景交接及 Presentation ready/suspend/UI ack 等条件决定。`desired.workspace=Desktop && applied.workspace=Desktop && !commandScenePending` 时正常刷新应开放，不存在 Setting visible 直接关闭此门的条件。

这排除了若干简化推断，但没有排除外部 RTS collector、COM 回调停顿、实际 HWND 可见性/层级错误或 Setting owner 同步命令不返回。此前当前 HEAD 的有限 GUI 基线成功往返同类步骤，也不能证明用户的时序/环境不存在问题。

### 仍保留的具体候选，以及未证明环节

**孤儿 contact + canvas command 屏障：** `Draw3.DrawingController.cpp:9831` 建立 command boundary，`:9882` 阻止后续出队，`:13163–13170` 要等 active 为空才处理命令并解除屏障。选择按钮在上述特定配置下的 Clear 可达这里。若旧活动笔画永久没有终态，后续不同 contact 与工具 marker 可能都排队。

这只是已知条件链，尚无“正常关闭 Settings/Explorer 必定丢失终态”的证据。`Draw3.RealtimeStylus.cpp:1170–1177` 在同 `(tcid,cid)` 再次 Down 时先取消旧 binding；`:1590–1609` 的 Disabled/Error/Tablet reset 收拢 contact。因此“漏一次 Up 就永远卡死”的推导不成立。`StylusOutOfRange`（`:1127–1134`）当前只清光标，但不能仅从此处缺少 Cancel 推导出本场景真实漏终态。

**RTS 停投递/内部等待或 decoder 拒收：** writer guard、生命周期 COM 查询和 decoder 准入是具体观察点，但没有新的现场栈或入口/出口证据，不能命名为已证明死锁。真实鼠标的绘制 contact 同样依赖 RTS，Win32 鼠标光标仍响应不等于画笔输入已经到达。

**工具发布前的 Setting owner 等待、输出握手未收敛：** 入口真实存在；仍需阻塞线程与 owner 线程栈或 desired/applied/revision 证据。不能仅凭设置曾被打开就归因。

### 最小现场判别证据

优先保留发生故障的同一进程，采用不依赖画布线程同步 stdout 的采样：

1. 程序精确版本/架构、诊断实际开关和输入来源；记录这一次是否确实同时尝试了触摸与鼠标，避免用不同用户报告拼成同一现场。
2. 间隔采样画布线程、RTS 回调线程、overlay/Setting owner 线程栈。区分线程无界等待和消费者持续运行但拒收。
3. 一次真实 Pen ↔ Eraser 切换的 UI mode revision、bridge revision/selection/tool、applied revision，以及主/辅助 Drawpad HWND 的 visible/enabled/styles/owner/capture。判断发布前阻塞、发布后未消费、输出未就绪或窗口不接收。
4. 同一笔真实 Down/Move/Up 的 RTS 到达、decoder/route 发布、admission revision、消费者读取/active/commandBoundary 状态。`published=1` 仅说明 mailbox 发布，不等于消费者接纳；旧 cursor 移动不能替代这些数据。

这是对现有调查方案的判别项收束，不是本轮已新增或执行的诊断功能。

### Files found

| 文件 | 本轮用途 |
| --- | --- |
| `Inkeys/IdtState.cpp` | 选择/画笔/橡皮状态入口、Setting owner 前置与 surface 握手 |
| `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp` | 真实按钮 clickFunc 与重复点击行为 |
| `Inkeys/Inkeys/Business/ComponentActions.cpp` | Explorer 外部启动入口 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` | bridge 发布的第一处 wake、状态消费与 admission |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp` | 工具 wake、激活/显隐消息与鼠标光标 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | command boundary、输出切换与活动 contact 处理 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp` | 重复 Down、终态收拢及 RTS 启停 |
| `Inkeys/Inkeys/Window/Window.cpp` | 两 Drawpad 表面的显隐与 capture 释放 |

### Related specs and prior research

- `.trellis/spec/native-desktop/index.md`、`cpp-conventions.md`：线程和所有权证据边界，最小修改。
- `.trellis/spec/native-desktop/draw3-integration.md`：Selection/Primary 显隐与版本握手，唯一主 HWND/RTS。
- `.trellis/spec/native-desktop/input-and-ink.md`：RTS contact 与 Win32 cursor 的区别，诊断开关与重定向输出边界。
- `research/input-permanent-stall-audit.md`、`canvas-input-path.md`、`settings-window-path.md`、`expanded-investigation.md`：复用已证实验及此前静态审计，不重跑、不扩大其结论。
- External references: 本轮没有新增外部查询；上述判断依据本地当前代码与既有实验，未新增对 Windows RTS 系统保证的假设。

## Caveats / Not Found

- 本轮只写本文；未修改产品、任务状态、其他研究文件，也未启动 GUI、构建或运行探针。
- 没有发现新的“该操作链一触发就永久卡住”的完整证明。缺少用户失败现场，不能以修复 F1/F2/F3 替代原问题定位。
- 有效 Up/工具 wake 是 F3 的恢复证据，不能保证解除任何外部线程阻塞或孤儿 contact command boundary；后者必须单独建立原因链。
- 该用户的具体输入设备仍未知；不同报告涉及触摸和鼠标，并不证明同一进程故障同时影响两者。

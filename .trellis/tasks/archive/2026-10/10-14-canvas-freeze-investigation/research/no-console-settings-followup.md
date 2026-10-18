# Research: 无控制台反馈与 Settings / Explorer 路径复核

- Query: 用户称没有开启任何控制台输出，按“画几笔 → 设置开关 → 从 Inkeys 打开 Explorer → 点击选择关闭 → 笔、橡皮失效”操作，是否属于已验证 F1/F2/F3；普通窗口路径是否会遗留 owner、暂停、穿透或输入禁用状态。
- Scope: internal；仅复核已有调查与当前生产源码；不修改产品、不构建、不运行 GUI。
- Date: 2026-10-15（Asia/Shanghai）
- Task: `.trellis/tasks/archive/2026-10/10-14-canvas-freeze-investigation`
- 最新补充: 这位用户的输入设备未知；总体报告触摸较多，也有鼠标。不能把总体分布直接赋予这位用户，不能以 Touch-only 实验解释所有鼠标报告。

## Findings

### 1. 本轮归因结论

现有 1/2/3 是各自限定条件下已验证的代码缺陷，不是已确认的这位用户的根因。新反馈应使 F1 在本案例中的优先级显著下降。按照目前描述，没有一条已完成证据链能把 Settings / Explorer 顺序直接接到 F1、F2 或 F3 的确定触发条件。

| 既有问题 | 对这份反馈的解释力 | 修复信心与范围 |
| --- | --- | --- |
| F1：同步诊断 sink 阻塞绘制线程 | 普通 Release、诊断关闭时，已验证的 Cursor flush 路径被开关切断；不能假设隐藏管道来强行匹配“无控制台输出” | 缺陷机制与恢复实验明确，有把握针对阻塞 sink 做修复；需保证输出线程取消与退出也有界，但不能承诺因此修好该反馈 |
| F2：PPT 冷槽读取 IoError 被误归 SourceChanged | 描述没有 PPT 已保存墨迹恢复，缺少必要场景；普通桌面不能直接套用 | 错误分类及现有重试路径明确，修复方向明确；只针对相应 PPT 恢复和可恢复错误，不放开未加载墨迹的数据保护 |
| F3：Speed 橡皮 idle 后 Move 无唤醒 | 当前叙述没有“同一次触摸接触中静止后移动”，而是操作后笔橡皮都失效；真实 Up / ControlWake 可解除已验证等待，不能解释有效终态和工具切换之后持续全失效 | 原码组件实验明确，能针对 idle / Move 通知协议修复；真实 Touch / Pen 及不同输入源仍要回归，不能把此项称为跨工具永久卡死的已知根因 |

因此，“有把握修复三个限定缺陷”与“有把握解决当前用户问题”必须分别回答。后者目前尚无足够证据。没有证明新的普通操作必现死锁或永久等待闭环。

### 2. 没有控制台输出与 F1 的具体边界

- `Draw3.Diagnostics.cpp:574–576`：`FlushCursorDiagnostics()` 在 `CursorDiagnosticsEnabled()==false` 时立即返回，不进入该 flush 的同步输出。
- `IdtMain.cpp:2614–2640`：Cursor 诊断来自持久化开关；非 `IDT_RELEASE` 构建另外允许 `INKEYS_DRAW3_WIN7_DIAGNOSTICS=1` 临时覆盖。普通发布构建没有该环境覆盖。
- `IdtMain.cpp:2631–2635`：Draw3 / TouchArea / Cursor 开关之一打开才在这段启动逻辑中初始化共用调试控制台。
- **例外，不能写成“关闭开关后没有任何 stdout 写入”**：`Draw3.Diagnostics.cpp:655–671` 的 `LogFrameTiming` 在 `_DEBUG` 下仍每至多 250ms 调用 `WriteFastConsoleLine`，不受 Cursor 开关约束。它仍需有效且受阻的输出 sink 才会出现同类阻塞。`_DEBUG` 与 `IDT_RELEASE` 是两个分别使用的编译条件，不应混为一个开关。
- “没有看到控制台窗口”与“诊断实际关闭、无有效输出端”不是逻辑同义，但没有现场证据就不应引入隐藏 pipe / 特殊启动器假说。按用户明确说未开启任何输出，应先以普通配置为调查基线，把 F1 降权。

### 3. Settings 打开、关闭没有直接暂停整个画布

- `Setting.cpp:8848–8869`：`Show` / `Hide` 更新的是 `settingSessionState` 可见性，入队对应 Setting business 命令，并请求 Settings 渲染；没有在这里更改 Draw3 admission / selection / workspace 或启用暂停标记。
- `Setting.cpp:262–267`：业务队列只调用 Window Service 对 `WindowRole::Setting` 的 Show / Hide。
- `Setting.cpp:647–658`：窗口关闭与任务栏关闭都转为 `Hide`，并不执行 `Shutdown`。`Shutdown` 中的等待不能直接套用到普通“关闭设置”步骤。
- `Window.cpp:358–362`、`:477–506`：Show 前准备 owner 链的 Z 序。代码明确禁止 Setting owner thread 等待 overlay 根抬升，避免该条同步逆向边。
- `Window.cpp:1672–1692`：Show 恢复/显示 Settings，调整其顺序并把前台、激活和键盘焦点交给它。
- `Window.cpp:1733–1741`：Hide 对目标 Setting HWND 执行 `ShowWindow(SW_HIDE)`，没有显式把 Drawpad 设为 disabled 或 click-through。

注意：上面证明的是应用显式状态逻辑；不能据此证明 Windows/RTS 的跨线程焦点、owner 和输入队列在所有设备上都无异常。

### 4. Explorer 没有应用侧挂起/恢复事务

`Business/ComponentActions.cpp:53–55` 的 Explorer 分支只执行 `ShellExecuteW(nullptr, L"open", L"explorer.exe", ... , SW_SHOWNORMAL)` 后返回。`Bar.Button.cpp:784` 的 built-in action 回调直接调用这一入口。它没有开始模态对话框，也没有“启动外部程序时暂停画布，退出后恢复”的成对业务逻辑。因此当前没有找到可由 Explorer 错误返回导致的应用暂停标记泄漏。

Explorer 出现仍会改变系统前台/焦点。该关联值得现场验证，但仅凭此序列不能判定 RTS 必然失效。

### 5. 选择 / 重新选笔走独立状态发布与窗口握手

- `IdtState.cpp:1104–1121`：选择按钮在拆分模式且有内容时可能先发布 Clear；拒绝时保留原工具；接受后通过版本检查进入 selection 并 `SyncDraw3State()`。
- `IdtState.cpp:1248–1255`、`:1354–1366`：选笔、选橡皮显式设置对应非选择态并同步。
- `IdtState.cpp:723–746`：同步时先按工具模式应用 Setting owner，再发布 Draw3 state；`:215–229` 的 `selectionMode` 来源是当前电脑交互模式，非 Settings 可见性。
- `IdtState.cpp:85–115`：owner 使用期望/实际版本，过期请求不覆盖当前；失败不记为完成。`:1758–1761` 的 StateMonitoring 每 250ms cadence 复核未完成 owner。
- `Window.cpp:431–457`、`:1794–1845`：owner 命令在 Setting 线程执行，检查真实 HWND owner；后续准备失败有恢复原 owner 路径。这里修改的是 Settings 的 owner，不是把画布变成 Settings 的子窗口。
- `Host.cpp:1243–1276`：画布消费者读取 bridge state，把 selection 和 active tool 应用到 WindowController。
- `Host.cpp:550–564`、`:2197–2208`：PPT 输入暂停需要 Presentation target 身份和相关 ready 条件；不是“任何设置窗口打开就暂停”。普通桌面仍可能因 commandScenePending 等其他门禁受阻，但本序列没有证明该状态永久残留。
- `Draw3.WindowControl.cpp:1526–1528`：画布收到 `WM_SHOWWINDOW / WM_ACTIVATE` 时请求重新呈现，不在该分支直接设长期输入禁用。

### 6. 保留但未证实的候选，以及需要的区分证据

仍需区分：(a) 绘制线程卡在等待/外部调用；(b) RTS producer 不再产生活动输入；(c) producer 产出但 admission、未终结接触或 command boundary 不接纳；(d) bridge 仍为选择态或真实主 HWND 可见性/输出目标未恢复。

Settings owner 同步发生在 bridge 发布之前，所以“工具状态改变、跨线程窗口提交未完成、绘制侧保留旧状态”是可检查的调用顺序候选；**本轮没有证明这个窗口提交会被用户步骤无限阻塞，也没有新建必现模型**。既有 `window-permanent-stall-audit.md` 的未闭合等待边界继续有效。

针对现场应保存：同版本二进制/PDB 的绘制、RTS 和窗口线程栈，最后一次实际 Down/Up 与输入身份，desired/applied tool/selection revision，admission 与 command pending 状态，以及 Drawpad/辅助表面的 owner、visible、exstyle、capture、focus。输出必须不依赖画布线程同步 stdout，避免复现过程中引入 F1。未知输入设备不能被默认为触摸；单纯把界面仍能点、光标仍能动当作 RTS 正常证据同样不足。

## Files Found

| 文件 | 作用 |
| --- | --- |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp` | 设置可见性、关闭路径、独立业务队列与生命周期 |
| `Inkeys/Inkeys/Business/ComponentActions.cpp` | 内建 Explorer 启动入口 |
| `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp` | 内建动作回调与工具入口 |
| `Inkeys/IdtState.cpp` | 工具转换、Setting owner 版本化同步、Draw3 bridge 发布与重试 |
| `Inkeys/Inkeys/Window/Window.cpp` | 两类窗口线程间的命令、owner、Show/Hide 和层级核对 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` | 消费 selection/tool 与 PPT 输入准入条件 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp` | 窗口激活/显示事件后的重呈现行为 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Diagnostics.cpp`、`Inkeys/IdtMain.cpp` | Cursor 诊断开关与 Debug 输出例外 |

## Related Specs and Prior Research

- `.trellis/workflow.md`：本任务保持 planning；本次只有研究产物。
- `.trellis/spec/native-desktop/index.md`、`draw3-integration.md`、`rendering-and-ui.md`：窗口归属、独立画布线程、Settings/UI 渲染边界与证据等级。
- 本任务 `research/expanded-investigation.md`：F1 完整 GUI 与 F2/F3 组件实验的真实证据范围。
- 本任务 `research/window-permanent-stall-audit.md`：同步窗口/锁候选与尚未闭合的等待环。
- External references: 本轮没有增加外部事实或文献；只复核当前本地源码与已经落盘的实验，不借用通用 Win32 故事推定现场根因。

## Caveats / Not Found

- 未执行新的 GUI、构建或故障实验，没有新增可称为“触发就永久卡死”的普通设置/Explorer 复现。
- 用户输入设备未知；总体触摸居多但也有鼠标，不能用 F3 Touch 组件测试覆盖所有反馈。
- 产品源码未变。能明确修复各限定缺陷，不等于当前用户事件已定位；不承诺三项修复会消除此类全部报告。

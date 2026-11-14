# Research: 多设备拖动失效的 DM / RTS 前端复核

- Query: 触控板扩展及上一轮修复后全部拖动仍失效，上一轮关于 DM 输入归属的归因是否充分；当前 WindowControl 前端是否存在确定的全设备阻断。
- Scope: mixed（当前生产源码、既有任务证据、Microsoft 官方 API 文档与上游源码搜索；不编译、不 GUI、不改产品代码）
- Date: 2026-11-12

## Findings

### 1. 上一轮确定了 API 选项差异，未确定现场的唯一根因

当前 `Draw3.TouchpadInput.h:17` 已设置 `MANUALUPDATE | DISABLEPIXELSNAPPING | INPUT | EXPLICITHITTEST`；`Draw3.WindowControl.cpp:388` 的真实 Initialize 调用该 helper，`:399` 激活 manager，`:400` Enable viewport。因而不能继续用“当前遗漏 INPUT / EXPLICITHITTEST”解释此次未恢复。

Microsoft 对两个选项的约束有明确适用边界：

- `INPUT` 使关联 contact 的输入对 **UI thread** 可见。`SetInputMode(MANUAL)` 文档说明它与设置该位类似；因此“还必须另调 SetInputMode 才生效”也不是已证实缺陷。
- `EXPLICITHITTEST` 控制 **RUNNING / INERTIA** viewport 对新的 **WM_POINTERDOWN** 的自动命中。它不是对所有 Win32 输入、全部 viewport 状态的统一开关。
- 官方 Processing input 说明：静止 viewport 在应用不调用 SetContact 时，会将该 contact 的输入交给 UI，而不是仅因 manager Activate 就开始识别并消费所有 contact。
- 官方 Portal 说明键盘/鼠标可由应用通过 `ProcessInput` 显式转交。不能从 touch pointer 的自动命中规则直接推导传统鼠标左键必被自动截断；本轮未找到当前代码调用 DM ProcessInput 的路径。

因此，上一轮的 fake `AutomaticallyAssociates() { return !(options & EXPLICITHITTEST); }`（`Draw3.WindowControl.cpp:738`）只刻画一个选项位，省略 viewport 状态、pointer 类型及系统交付链；其 PASS / 旧策略负对照不能证实“全部设备原先在 OS 前端被 DM 吞掉”。这不否定保留两个选项的价值，但否定把模型结果当现场根因证明。

### 2. 当前非触控板分支不会主动 SetContact，但这一事实仍不等于证明 RTS 可达

`Draw3.WindowControl.cpp:2139–2172` 处理整类 DM_POINTERHITTEST，仅在模式/准入许可且 `GetPointerType` 明确 PT_TOUCHPAD 时关联；非目标返回 0。实际 predicate 是 `Draw3.TouchpadInput.h:26`。

`WM_POINTERDOWN` 后续入口（`Draw3.WindowControl.cpp:2332`）没有把 Touch / Pen 交 DM；`WM_POINTERCAPTURECHANGED`（`:2249`）只处理光标触觉离开及 control wake，没有直接调用 RTS Shutdown、终止 coordinator contact 或关闭其 publication gate。`SetContact` 也只出现在 owner 的 PTP Contact 入口（`:446`）。

这些源码事实支持“应用没有主动把普通来源关联给 DM”，不能单独证明 Windows 内部对同 HWND 的 RTS 注册没有任何影响。

### 3. RTS 是四类接触输入的共同上游，原日志没有越过这条边界

`Draw3.RealtimeStylus.cpp:2718` 使用 SetAllTabletsMode(TRUE)，因此正常 RTS 档的 Mouse / Touch / Pen 共用 RTS producer；该路径的 `WM_MOUSE*` 是光标路径，不能当作 contact producer。

- 真正 Down：`:1183` callback lease → `:1185` publication token → `:1188` Down arrival 诊断 → decoder → `:1269–1276` coordinator PublishDown → `:1287` published / publish-failed 诊断。
- 这些 Down 诊断走 `RecordInputDiagnostic` 的 Cursor gate，而不是仅开 CanvasPan gate 就必然出现。
- `:1454` 的 CanvasPan `rts-batch` 记录显式排除 Pen；0 条不能证明 Pen callback 没有发生。
- Win32Mouse fallback 是另一条实际 contact producer：`Draw3.WindowControl.cpp:1005` 的 mouseInput.Down，不能把 RTS 档光标消息过滤失败等同于 fallback contact 失败。

上一轮日志只说明没有成功 pan-register 和所有已消费位移来自 native；没有 RTS Down arrival / published / consumer 拒收的完整证据链，不能把阻断位置唯一锁定在 RTS callback 之前。

### 4. 没有找到足以宣称同 HWND 的 DM 与 RTS 必然互斥的 primary source

Microsoft 的 WPF 文档明确讨论 **RTS 与 WM_TOUCH** 在同 HWND 互斥；它没有声明 DM 与 RTS 必然互斥。Microsoft RTS Reference 说明同步 plugin 位于 RTS 专属线程；而 INPUT 文档承诺的是 UI 输入交付。这两者之间不能自动画等号。

本轮对 Microsoft 文档及 Chromium / Flutter 上游的检索没有找到“DM Activate 必然停止 RTS 虚拟鼠标/笔/Touch”的确定合同。不能把它当已证实根因，也不能因为没有找到文档就排除系统互操作问题。若其他共用 consumer 门禁没有确定缺陷，仍应在新产物完整现场日志中核对同一物理 Down 的 RTS 到达 / 发布及 consumer route，必要时进行有明确授权的最小系统对照。

### 5. PTP 偶发成功另有一个状态交付风险，尚不足解释全部设备

`TouchpadNavigationOwner::Content`（`Draw3.WindowControl.cpp:566–567`）在未确认 Running / Inertia 前只缓存变换；`Status`（`:529–532`）在未确认 phase 时忽略 Ready，`Interaction END` 最终可使 frontend 不再 Accepting（`Draw3.TouchpadNavigation.h:212–216`）。

Microsoft `AddEventHandler` 明确允许连续的同类 callback 合并。若一个短手势在 owner 被调度前已结束，系统未单独向应用交付 Running，而只交付 Ready / 最终 content，则当前 frontend 可能把真实短手势归为“初始 Ready / 未识别”，使缓存位移永不发布。当前代码未利用 Ready callback 的 previous 状态区分这一情形。已有无窗口探针固定手动发 Running，再发 Inertia / Ready（`Draw3.WindowControl.cpp:773–779`），没有检验系统省略中间状态的情况。

这是 **基于允许合并的 API 合同发现的状态处理风险**，不是已在本机真实 PTP 回调中复现；其影响限于触控板某些手势，不能独立解释普通鼠标/Touch/Pen全部失效。不要把它替换成新的唯一根因。

### 6. 已排除或另列的生产接线检查

- 真实 WndProc 是 `Draw3.Product.cpp:330`，不是历史 IdtDrawpad.cpp；running 时没有消息白名单，全部转发 `ForwardProductMessage → Host::ForwardMessage → HandleExternalMessage`。
- Product 层 mutex 是 recursive_mutex（`Draw3.Product.cpp:15`）；不能仅凭同步 COM 可重入就判定该锁必死锁。
- 新消息 `WM_APP+0x3D5` 在生产 WindowControl 没有发现重复定义；HiddenWindowTest 的同值专用于其测试窗口。
- 停止阶段只有 mouse owner stop 获得 ProductRunning / productStopping 豁免，DM stop 没有。这是另一个退出路径问题，不是 running 期间全部拖动的解释；本轮不改。

### 7. 用户再次纠正与新日志：不能再假设退出移动模式后 RTS 正常

早先转述的“同次切回画笔能正常画线”已被用户纠正，不作为当前事实。当前反馈是：重启后不碰触控板，Mouse / Touch / Pen 仍完全不能拖动；尝试后切回画笔也不能画。Pen hover 可跟随，Down 后光标固定 Down 点且无墨，Up 后恢复跟随。

`Draw3.RealtimeStylus.cpp:1180–1288` 的 StylusDown 不读取 SelectionMode / CanvasPanMode，producer 发布没有移动模式专属判断。另一方面，`Draw3.WindowControl.cpp:2398–2420` 直接从 WM_POINTER 的 Pen 坐标发布光标样本，因此 hover 活着不能证明独立 RTS producer 活着。

主会话直接从用户粘贴的完整启动日志核对（没有本地附件）：

| 事件 | seq / QPC | 可证明的事实 |
| --- | --- | --- |
| RTS Extended ready / input gate open | 启动段 | RTS 成功选定，非启动降级失败 |
| 多 context 的 rts-batch published=1 | seq2–680；QPC240479279868–240494105709 | DM 初始化最终结果之前，真实已解码非 Pen Packets 可写入 coordinator |
| 首次 touchpad-init unavailable | seq681；QPC240498149214；UI tid22996 | Initialize 某阶段返回 0x80070005；当前仅记录最终 HRESULT |
| leave-move-mode | seq682；QPC240587869978；drawing tid25496 | 绘制线程之后仍能处理模式边沿 |
| 第二次 touchpad-init success | seq683；QPC240685325006；UI | 当前修复版明确配置初始化成功 |
| leave-move-mode | seq684；QPC240693019452；drawing | 初始化成功后绘制线程也未整体停死 |

日志 QPC frequency 为 1e7；seq680 到 seq681 相隔约 0.404 秒。seq681 后直到日志末尾没有 rts-batch，整个日志没有 pan-register / pan-reject / touchpad-contact / touchpad-hit-test。**不能把 seq2–680 的 published=1 说成 DM 初始化后 RTS 仍持续工作。** 这条时间关系把首要候选推向首次 DM Initialize / 失败清理对同 HWND 的输入注册影响，而不是“接触已入 consumer 后全部被 NativeRunning 拒绝”。但 rts-batch 位于 callback 参数检查、state guard、binding / decoder 解码之后，且排除 Pen；日志缺失本身仍不能唯一判断 callback 根本没来、还是早退。

另一运行的 Cursor 片段（PID34224、owner21728、drawing10892，从seq24193开始）包含画笔 selection=0 / pan=0 下完整 Mouse Down / Move / Up（seq24925–26038）均 accepted=1，随后 Touch pointer 完整生命周期；持续有 drawing frame 日志，但没有 rts-input / rts-batch。这进一步支持“独立窗口消息与光标路径活着，RTS contact producer 未建立”的定位，片段缺少 DM 初始化前半，不能用于断言 RTS 从何时中断。`mouseLifecycle=0/0` 实为 SpeedEraser.HasPosition / NeedsAnimation，不是真实 contact 生命周期；`touch-begin tracked=0` 是 Pointer 只做视觉屏障，不等于 RTS 活动触点统计。这两个字段不能被额外拿来证明 contact 消失。

### 8. E_ACCESSDENIED 的可定位范围及退出模式后的持续性

`TouchpadNavigationOwner::Initialize` 当前把多个 COM 调用共用一个 result，`:403` 只记录最终值。0x80070005 **不能定位为已知的 Activate 或 CreateViewport 失败**。按源码可排除三个来源：

- CoInitializeEx 失败在 `:382` 直接返回，不会经过该条 touchpad-init 日志。
- GetClientRect / 空 rect 失败在 `:390` 被显式置为 E_FAIL。
- Make Handler 为空在 `:396` 被显式置为 E_OUTOFMEMORY。

尚不能区分的调用包括 CoCreateInstance、GetUpdateManager、CreateViewport、ConfigureTouchpadViewport 内的 ActivateConfiguration / SetViewportOptions、SetViewportRect、GetPrimaryContent、AddEventHandler、manager Activate 和 viewport Enable。Microsoft 对相关方法公开文档主要给通用 HRESULT 失败合同，没有找到能把此值唯一映射到某个方法或 RTS 互斥的 primary source。它也不能直接证明文件权限、进程提权或 DCOM 配置问题。

失败后 `:404` 立即 Shutdown；其中 Stop / Disable 不检查结果，接着按实际注册标记 RemoveEventHandler / Abandon / Deactivate。清理失败则保留 partial owner；完全成功才释放资源及本 owner 对应的 COM 初始化计数。当前日志没有清理分阶段结果，不能判断首次失败之前系统 HWND 输入注册是否已改变、清理后 RTS 是否恢复。第二次能够创建并成功初始化新 owner，说明前次没有一直卡在保留 partial owner 的分支，不能据此推导 OS 端输入注册已经恢复正常。

成功初始化后 manager 在 `:399` Activate，通常退出移动模式的 `Apply`（`:421–435`）只 Stop / Disable / Reset viewport，不 Deactivate manager；真正 Deactivate 位于 owner Shutdown（`:473`）。Microsoft 说明 Activate / Deactivate 控制 HWND 输入处理且引用计数。因而 **若** 问题发生在 manager / HWND 注册层，仅退出移动模式 Disable viewport 不足以验证已恢复 961ffdf8 的系统输入状态，这与“回画笔仍坏”的持续性相容。此为源码确定的生命周期差异及高优先候选因果链，尚无 OS 对照证明其就是根因。

## Files Found / Code Patterns

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.TouchpadInput.h`：实际 viewport 参数与 PT_TOUCHPAD 关联 predicate。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp`：真实 COM 生命周期、消息分流、系统 callback 与 fake viewport probe。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.TouchpadNavigation.h`：candidate / phase / interaction 接纳状态。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp`：四类设备共同 RTS producer、Down 到达与发布证据。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Product.cpp`：真实 WndProc 和 Product gate / 生命周期锁。
- `.trellis/tasks/11-08-chromium-touch-fling/research/multi-input-regression-log-analysis.md`：旧日志边界；不得把旧候选归因重复当本轮结论。

## External References

- [Microsoft Viewport options](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/ne-directmanipulation-directmanipulation_viewport_options)：INPUT 与 EXPLICITHITTEST 的适用边界。
- [Microsoft SetInputMode](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationviewport-setinputmode)：MANUAL 与 INPUT 位的关系。
- [Microsoft Processing input](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/directmanipulation/directmanipulation-processing-input-with-directmanipulation)：静止/运行 viewport、SetContact 与默认交付。
- [Microsoft Direct Manipulation portal](https://learn.microsoft.com/en-us/windows/win32/directmanipulation/direct-manipulation-portal)：鼠标/键盘需应用转发 ProcessInput。
- [Microsoft AddEventHandler](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationviewport-addeventhandler)：owner thread callback 及 callback 合并许可。
- [Microsoft RTS Reference](https://learn.microsoft.com/en-us/windows/win32/tablet/realtimestylus-reference)：同步 plugin 专属线程和 HWND 范围。
- [Microsoft Disable RTS for WPF](https://learn.microsoft.com/en-us/dotnet/desktop/wpf/advanced/disable-the-realtimestylus-for-wpf-applications)：仅明确 RTS / WM_TOUCH 互斥，不能类推 DM。
- [Microsoft CreateViewport](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationmanager-createviewport)：HWND 关联与通用 HRESULT 合同，没有指定 E_ACCESSDENIED 的唯一成因。
- [Microsoft Activate](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationmanager-activate) / [Deactivate](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationmanager-deactivate)：manager 控制 HWND 输入处理，调用引用计数。
- [Microsoft Abandon](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationviewport-abandon)：释放 viewport 资源；没有说明它自动重建既有 RTS 注册。

## Related Specs

- `.trellis/spec/native-desktop/input-and-ink.md`：RTS / Win32Mouse 唯一 producer、诊断 gate 和绘制 contact / 光标区别。
- `.trellis/spec/native-desktop/canvas-pan.md`：DM 前端和真实设备验收边界；部分措辞比目前 OS 实测证据更强，应在根因确认后校正。

## Caveats / Not Found

- 本报告没有给出“DM 前端已经确定是全部拖动失效的唯一根因”；当前上游源码和旧日志不足以支持该结论，必须与 consumer 共用准入链的独立调查合并。
- 没有 GUI、Windows/RTS 输入注入、编译、测试或修改产品代码；只新增本研究文件，没有改动测试、规范、既有记录，没有执行 Git 操作。
- 角色范围禁止任何 Git 操作，基线差异由主会话负责；本报告的行号指本轮读取的当前文件。
- 工作区当前没有 `Build/Logs` 目录，故未把旧文档所列构建日志当作本轮重新读取的运行证据。
- 尝试只读枚举 Microsoft-Windows-DirectManipulation 事件日志时，Get-WinEvent 返回 unauthorized operation；未提权、未启用事件通道、未修改系统配置，未取得内部失败阶段证据。

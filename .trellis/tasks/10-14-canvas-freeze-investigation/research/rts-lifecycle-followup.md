# Research: RTS lifecycle、writer gate 与跨线程 owner 后续调查

- Query: 在旧 Debug 副本完整操作链仍能绘制的基础上，继续核对 RTS producer 的 writer gate/COM 重入、UpdateMapping/Disabled/Error 恢复路径，以及 Setting 跨线程 owner 和 attached input queue 的具体候选；查明 10 月 11 日视频是否已有 rts-input 日志。
- Scope: mixed；本地产品源码 + 微软官方 API 文档/工程师说明。Git 历史由主会话执行只读查询并提供结果，研究代理未执行 Git。
- Date: 2026-10-15
- Active task: `.trellis/tasks/10-14-canvas-freeze-investigation`
- Boundary: 仅新增本研究文件；未改产品代码、未做 GUI 操作或构建。延续 `canvas-input-path.md` 的输入链和证据口径。

## Findings

### 1. 目前能收敛与不能收敛的结论

- **不能据源码认定 writer gate 已存在必现死锁。** 当前 RTS plugin 正确聚合 FTM；写侧 RAII，读侧 try-enter；所检查 sink 没有同步调用 Window Service，也没找到持有 reader 后反向请求 writer 的应用路径。
- **UpdateMapping 和 Error 不是显然忘记复位。** Mapping 的 rebuild 会重新 BeginLifecycle；Error 保留 decoder；坏 Up 包会主动 Cancel。仅一次 transient rebuild 失败不会在代码上永久锁死后续 Down 的补建机会。
- **明确的观测缺口：** Cursor 产品日志覆盖 Down/Up/Packets，却不覆盖 Lifecycle writer 的进入/取得锁/查询完成/离开；不能区分“RTS 没再调用”与“已卡在无日志的 Mapping/Tablet writer 中”。`IsInitialized()` 也只回报启动标记，不回读实际 COM Enabled。
- **Owner/queue 关系是实际边界，但尚无 RTS 根因证据。** Setting 和 Drawpad 确属两个 window thread，运行期建立/解除 owner；微软明确说明跨线程 owner/owned 会自动附加输入队列。公共资料没有说明仅此关系变化必然损坏 RTS collector，因此只能保留为条件候选。
- 主会话报告旧 Debug 副本在划线 → 设置打开（白屏但可关）→ Explorer → 选择 → 关 Explorer → 软笔划线全过程仍有 `Down/Up published=1`。这证明该运行未复现输入失效；不能把设置白屏与画布卡死自动合并成一个根因，也不能用旧副本的正常结果替代 HEAD 测试。
- 视频后续证据：约74秒 `Build/CanvasFreezeInvestigation/video/end-01.jpg` 旧红墨迹已清除，随后软笔仍无新墨迹；约50秒后曾出现橡皮光标而擦除无效。它削弱“所有canvas command自50秒起一直被旧active阻塞”，若确为Clear执行则反证该时刻命令链完全停滞。当前“RTS交付/先前callback占用 → no-decoder → admission”仅是工作假说的调查优先级，不是已证因果链。

### 2. Files found / 证据入口

| 文件 | 重点 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:82` | reader gate、writer RAII；`1021` plugin/FTM；`1086` Enabled；`1102` Disabled；`1137` Down；`1241` Up；`1416` TabletAdded；`1442` TabletRemoved；`1455` Error；`1473` Mapping；`1690` decoder 重建。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:727` | RTS sink 的 Touch/Cursor 通知；`935` 系统光标只 PostMessage；不直接同步请求 Settings/Window Service。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cpp:197` | Cursor mailbox 短 writer latch，仅原子字段复制，不调用 COM/USER32/业务回调。 |
| `Inkeys/Inkeys/Window/Window.cpp:230` | overlay/setting 两只 jthread；`431` owner API；`477` Z 序准备；`870` 独立消息循环；`1376` 跨线程 Submit；`1794` 执行 owner 修改。 |
| `Inkeys/Inkeys/Window/Window.CommandCompletion.h:16` | 无 deadline 的 Submit 最终 `future.get()`，不泵消息；属于既有窗口线程等待边界，不在 RTS plugin 内调用。 |
| `Inkeys/IdtState.cpp:723` | 工具同步先发布/应用 Setting owner，再发 Draw3 属性；选择/画笔切换会建立/拆除跨线程 owner。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:6271` | Raw Input 只用 RIDEV_INPUTSINK；没有 RIDEV_NOLEGACY，不支持“它直接屏蔽全部传统鼠标”这个推断。 |

### 3. writer gate 的完整锁范围和已排除的伪等待环

`RtsStateWriterGuard` 在 `Draw3.RealtimeStylus.cpp:114` 先取得 `stateWriterMutex_`，`116` 置 writer bit，`117` 等待已有 reader 归零，析构 `123` 清 gate，成员析构再释放 mutex。

| 入口 | 写侧持有期间的行为 | 容易误诊之处 |
| --- | --- | --- |
| Enabled `1086` | 取消旧 contacts/reset cache → BeginLifecycle → shared scale + 全量 decoder 构建。 | 首次/重新启用时可能同步执行 COM 查询；不是单纯置一个 bool。 |
| Disabled `1102` | 取消旧 contacts → cache.Reset → 清 Pen visual。 | 不在此重新 Enable 是符合 Disabled 语义的；关键是是否确实发生了非退出的 Disable 且无后续 Enabled。 |
| InRange `1115`、Down `1137` | cache hit 直接返回；miss 才补建 scale/context；Down 还更新 binding、publish contact。 | Down arrival 在锁前打印（`1141`）；arrival 后无结果可缩小到锁/decoder/publish，不等于“RTS 根本没进”。 |
| Up `1241` | 读已锁存 binding，解码或 Cancel，擦除 binding。 | 无 packet、无 decoder、坏 packet 都有 Cancel 路径（`1256`、`1269`）。 |
| TabletAdded `1416` | GetTabletContextIdFromTablet + decoder 增量构建；失败则 reset + rebuild。 | 增量失败不会保留已废弃 generation 混用；返回 S_OK 不能证明重建成功。 |
| TabletRemoved `1442` | 清 visual、取消旧 contacts、reset、source 非空时 rebuild。 | rebuild 第一行 BeginLifecycle，不能只看到前一个 reset 就断言永久 disabled。 |
| Error `1455` | 清 visual、只取消 active，不 reset decoder。 | 当前代码没有“Error 后 decoder 被禁用而永远无法落笔”的明显错误。 |
| UpdateMapping `1473` | 清 visual、取消旧 contacts/reset、重建当前 contexts。 | 有重启 lifecycle；但没有产品运行期开始/结束/失败日志。 |

写侧内的可阻塞外部 API：

- `GetAllTabletContextIds`（`1620`）、`GetPacketDescriptionData`（`897` / `1645`）、`GetTabletFromTabletContextId`（`916`）、IInkTablet QI/DeviceKind/HardwareCapabilities（`920` 至 `939`）。
- `ReadPointerSources()`（`422`）在 decoder 构建时还会调用 `GetPointerDevices/GetPointerDeviceCursors/GetPointerDeviceRects`，并创建 vector/shared_ptr（异常在函数内 catch）。它们是 USER32/设备枚举边界，不能假设永不等待；目前也没有现场证据说明它们等待某只 owner queue。

读侧只有 Packets、InAir 和 TouchArea cache snapshot。`RtsPacketStateGuard`（`82` 至 `107`）遇 writer bit/CAS 竞争就不进入，析构减 reader；不会等待 writer。Packets 在 reader 期间更新 coordinator 和 cursor mailbox；sink `WindowControl.cpp:806` 只改内存、发 control wake、异步 PostMessage。cursor mailbox latch 在 `PenCursor.cpp:200` 至 `218` 内不调用外部对象。TouchArea snapshot 持 `diagnosticsMutex` 借用 plugin，但 Shutdown 在调用 `put_Enabled(FALSE)` 之前已释放这把锁（`RealtimeStylus.cpp:2711` 至 `2717`）。没有发现这些私有锁之间的确定反向链。

**仍有条件性的死锁形状：** 如果某个 writer 内 COM/USER32 调用实际同步回调同 plugin 的另一个 writer，同线程将递归等待非递归 mutex；若它等待另一个线程上的 writer，则会形成跨线程环。必须证明现实 API 路径确实发生这种重入。不能仅因为一个 mock 能触发重入，便宣称 Windows/RTS 必然如此；更不能先把 mutex 换 recursive 来当修复。

### 4. 生命周期恢复状态矩阵

| 事件与中间失败 | 返回后 cache 状态 | 后续 Down 的机会 / 持续失败条件 |
| --- | --- | --- |
| Enabled 构建全部成功 | enabled、scale有效、decoder有效。 | cache hit，无硬件补查。 |
| Enabled staging 某一步失败 | 已 BeginLifecycle，可能 scale/decoder为空。 | Down/InRange `EnsureContextDecoder`（`1723`）会补查，若查询恢复可自愈。 |
| UpdateMapping/TabletRemoved rebuild 查询失败 | Reset 后 BeginLifecycle（`1706`），仍 enabled；scale/decoder未完成。 | 与上行相同；只有每次查询持续失败/阻塞才保持失效。 |
| UpdateMapping 查询得到 contextCount=0 | BeginLifecycle 后返回 true（`1710`），没有 decoder。 | 新 Down 的补建会重查当前 contexts/scale；不是不可逆空态。 |
| TabletAdded 增量失败 | reset + rebuild；rebuild 总会先 BeginLifecycle。 | 可由 Down/InRange 补建；应输出增量及全量真实 HRESULT。 |
| Error | 当前 active 被 Cancel，decoder generation/lifecycle保留。 | 下一个 Down 可重绑；现有 `RtsErrorPreservesDecoder` 类测试 helper 在 `2318` 至 `2372` 断言这部分 CPU 行为。它未实际调用真实 COM Error callback。 |
| Disabled | lifecycle=false、scale无效、decoder清空。 | `EnsureContextDecoder`（`1729`）拒绝补建；只能由真正 Enabled/显式 rebuild 开新 lifecycle。符合关闭语义，但无后续Enabled时会持续无输入。 |
| 超过固定 decoder capacity、首个 context 的 scale 查询一直失败 | 整体重建失败；fallback shared scale 也继续失败。 | 同机其它 digitizer/virtual tablet 的坏数据可能阻止当前设备补建；需记录 contextCount、首个tcid、requested tcid和 HRESULT，不能只输出 no-decoder。 |

`QuerySharedPositionScale` 固定取 `GetAllTabletContextIds` 的第一个 context（`1656` 至 `1662`），`RebuildCurrentContextDecoders` 同样用 `contextIds[0]`（`1713`）。因此“目标 Down 的 context 可读，但排序第一的另一个 context 持续不可读”是一个具体可验证的补建失败候选；当前没有证据表明它在设置/Explorer 场景出现。`BuildStagedDecoders` 逐个构建，一项失败整批不发布（`1683`）。这与 writer 永久卡住是不同故障类别。

实际 COM Enabled 与本地 initialized 也不同：`RealTimeStylusInput::IsInitialized()`（`2814`）只返回 `impl_->initialized`，它仅在初始化成功/Shutdown维护；它不能确认 callback 是否仍活、COM Enabled 是否true或decoder是否有当前设备。

### 5. Setting owner / attached input queues：具体且有限的因果链

1. `Window.cpp:230` / `235` 创建 overlay 与 Setting 两只线程；Drawpad 在 overlay group，Setting 在另一 group。
2. `IdtState.cpp:741` 按非选择态发布 `SettingOwnedByDrawpad=true`，`744` 应用。选择态拆除；该策略不以设置窗口当前是否显示为前提。
3. `Window.cpp:1813` 在 Setting thread 用 `SetWindowLongPtrW(GWLP_HWNDPARENT)` 修改 top-level popup owner 为主 Drawpad，选择时设空。微软工程师明确说明 owner/owned 跨线程关系会自动合并 input queues；焦点、激活、捕获等操作由此存在同步依赖。[Sharing an input queue…](https://devblogs.microsoft.com/oldnewthing/20130607-00/?p=4143)
4. `PrepareSettingZOrder`（`Window.cpp:501`）已认识到根置顶可能同步发消息，Setting thread 不能等 overlay；其它线程仍可能通过 `Submit` 的 `future.get()` 等待命令。窗口线程必须继续泵消息；当前 `RunGroup`（`911` 至 `939`）在 hidden 后仍继续 MsgWait/Peek/Dispatch，没有“关闭设置就停掉消息循环”的代码。
5. Draw3 RTS 初始化绑定的是主 HWND（`RealtimeStylus.cpp:2531`），主 HWND没有因为设置开关而改变。公开资料不保证内部鼠标 collector 对 input queue 拓扑变化的具体实现；**不能从上述自动附加行为直接推出 collector 已损坏**。

可以明确区分两种现场：

- 若 attached threads 真形成 UI同步等待环，overlay/setting消息心跳及对应焦点/显隐操作会停；同一时段 Drawpad accepted 新日志通常也停。此类现象由窗口/UI研究继续负责。
- 若 overlay/setting仍泵消息、真实鼠标 WM_MOUSE进入Drawpad、controller帧仍推进，却没有 RTS Down arrival，问题范围才缩到 RTS collector/事件交付或既有 callback 长时间占用。此时要看RTS callback线程栈和lifecycle最后阶段；重复设置owner/topmost只是试探恢复，不是根因证据。

### 6. 微软官方文档核对：禁止的推断与可用事实

- [Threading Considerations for the StylusInput API](https://learn.microsoft.com/en-us/windows/win32/tablet/threading-considerations-for-the-stylusinput-apis)（页面更新2021-01-07）：sync plugin一般在RTS线程；Enabled/Disabled可在修改Enabled或增删插件的线程执行，Error在插件抛错的线程执行；sync callback若同步等待其它线程可阻断ink收集。因此不能假定所有callback天然在一只thread上串行，也不能仅看UI活来排除RTS线程被阻塞。
- [GetPacketDescriptionData](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-getpacketdescriptiondata)（2024-02-22）提供在StylusUp内调用查询的示例；[UpdateMapping](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-istylusplugin-updatemapping)（2024-02-22）建议查询当前contexts，并说明显示DPI/方向变化及WM_DISPLAYCHANGE触发。**sync callback里调用查询本身不是已证实违规；普通Setting show/hide也不能自动当成Mapping触发。**
- [RealTimeStylusDisabled](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-istylusplugin-realtimestylusdisabled) 与 [RealTimeStylusEnabled](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-istylusplugin-realtimestylusenabled)（均2024-02-22）：分别与对象disable/remove plugin、enable/add plugin对应。当前产品只在初始化put_Enabled(TRUE)、Shutdown put_Enabled(FALSE)，没有Settings常规toggle。不能假设打开设置按公共合同就应该触发Disabled。
- [AddStylusSyncPlugin](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-addstylussyncplugin) 要求plugin聚合FTM；产品 `RealtimeStylus.cpp:1030` 创建FTM、`1038`委托IMarshal、`2654`附近检查结果，符合这一点。[COM and Automation Threading Considerations](https://learn.microsoft.com/en-us/windows/win32/tablet/com-and-automation-threading-considerations)说明MTA/FTM仍须自行线程同步；其中旧InkCollector/InkOverlay的特殊销毁/异常条目不能不加区分套到RTS。
- [SetAllTabletsMode](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-setalltabletsmode)（2024-02-22）：TRUE允许鼠标+stylus；应在Enabled之前设置，Enabled后修改会失败。产品顺序正确；不要把运行期重调此API当无代价诊断。
- [put_Enabled](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-put_enabled)（2024-02-22）：Disabled无事件；同一HWND上的多个Enabled RTS若输入矩形重叠行为未定义。本产品唯一Host/producer，不应为验证临时给主HWND再绑一套RTS。

### 7. Git历史核对（主会话提供，未独立执行）

主会话已执行只读历史查询并向本研究回传：

| 时间/提交 | 已提供的事实 | 结论边界 |
| --- | --- | --- |
| `fc9a8b86`，2026-08-16 13:45:50 +0800，`feat(draw3): integrate Draw3 runtime` | `-S stateWriterMutex_`、`-S RtsStateWriterGuard`、`-S Error只终止当前contact` 均只命中该提交。 | writer gate 与Error保留decoder的恢复语义自产品集成起即存在，不是10/11之后的新修复；未据此证明没有旧问题。 |
| 2026-09-14 的 `cb38173d` / `97f351b6`，2026-10-01 的 `9cce16bd` | RealtimeStylus 更早的变更节点。 | 尚未提供逐项diff，不能把某次归因为本问题修复。 |
| `21a37239`，2026-10-03 21:06:22 +0800，`Add Win7 input and GPU pixel diagnostics` | 引入 `rts-input event=`；该文件diff为71增5删：主要增加Down/Up arrival、no-decoder、published与Packets限频，并保存既有Close返回值用于日志；未改writer、decoder恢复、输入准入语义。该文件10/03之后无变化。 | 正常基于该提交后的10/11构建应含Down/Up arrival/published日志；还需验证视频EXE身份、Cursor开关与完整日志，滚屏看不到不等于未发生。 |

据以上历史，不能宣称“10/11之后已修好RTS卡死”，也没有证据支持把10/03的诊断日志新增当作恢复修复。已知是RTS源码在那之后未变，未复现也可能由二进制/驱动/时序差异导致；Window/Setting其它文件的历史由主会话另行综合。

### 8. 推荐的下一组最小诊断，仍不实施修复

#### 8.1 现有debug开关可以做到什么

| 现有能力 | 现在可作的区分 | 仍不能区分 |
| --- | --- | --- |
| `ConsoleOutput.Cursor` / `rts-input`（`RealtimeStylus.cpp:1141`、`1236`、`1244`、`1304`、`1535`） | Down arrival后出现no-decoder，说明至少该次callback已被交付、取得writer并完成decoder查找；published说明已写mailbox；arrival后无结果支持中间失败/阻塞。 | 没有arrival时，collector没投递、先前callback占用、实际Disabled、日志缺失仍混在一起；no-decoder不能区分本地lifecycle=false、shared scale失败或目标decoder失败；published不等于controller gate通过。 |
| `ConsoleOutput.TouchArea`（`Host.cpp:947`、`973`；`RealtimeStylus.cpp:2752`） | 可复制已有Touch缓存，区分“得到Touch快照”和“未得到/忙”。 | 不查询实际COM Enabled，也不枚举当前Mouse decoder；mouse-only下no-matching-touch正常。not-ready-or-busy也不能区分writer占用与无相关Touch。 |
| `ConsoleOutput.Draw3`（`IdtMain.cpp:2623`） | 实际present/资源/像素诊断可验证后端与呈现。 | 不查询RTS Enabled、当前decoder或callback活性；空dirty的cursor success=0仍非实际present失败。 |
| 旧 `DRAW3_RTS_DIAGNOSTICS` / `SetRtsTraceEnabled`（`RealtimeStylus.cppm:36`） | 编译启用时有部分旧callback trace。 | 产品vcxproj没有定义此宏，不是现有设置里的运行期开关；现有trace也没有全套Mapping阶段/外读快照。不能建议用户只开某个现有开关便获得这些字段。 |
| Runtime metrics / ContactInput计数（`Host.h:195`、`Host.cpp:1481`） | 显式启用的测试/metrics host可累计输入发布/回收。 | 正常产品 `IdtMain.cpp:3216` 的options没有启用它，默认false；配置ConsoleOutput四开关（`Other.Config.cppm:356`）没有RuntimeMetrics字段。普通运行全0不构成证据。 |
| `IsInitialized()`（`RealtimeStylus.cpp:2814`） | 启动/Shutdown的本地标记。 | 不是实时get_Enabled，也不是producer/decoder健康状态。 |

因此，**现有debug可先把一次Down分到arrival/no-decoder/published，但完整区分enabled/decoder/collector必须补诊断。** 即使补本地lifecycle镜像，字段也必须叫observed-enabled/local-lifecycle，不能冒充实际COM Enabled。缺少callback时仍需外部观察其最后阶段；必要时另取线程栈或受控COM只读查询。

#### 8.2 可审查的最小实现范围（建议；未写代码）

目标只观察视频这种“Win32/绘制帧仍活”的失效。复用现有Cursor开关、有界队列、Host运行快照和StateMonitoring既有250ms cadence，不新建线程/计时器，不发新增control wake，不改变锁、contact、配置、工具或恢复行为。

| 建议文件 | 最小改动职责 |
| --- | --- |
| `Draw3.RealtimeStylus.cpp` | 在RTS impl生命周期内一次性拥有固定大小的原子诊断状态，plugin只借用该状态且借用结束先于impl销毁。记录lifecycle entry/exit、writer waiting/acquired/query/exit阶段、序号/TID/QPC、已观察Enabled/Disabled、Down arrival/completed、cache generation/local-lifecycle/scale-valid/decoder-count、最后查询kind/HRESULT/requested与first tcid。Packets仅廉价计数，继续原每秒限频，不逐packet格式化。不得借诊断改变原RAII/reader gate。 |
| `Draw3.RealtimeStylus.cppm` | 增加一个const、非阻塞的诊断快照/trace入口，仅读impl拥有的诊断原子状态；不能去取得stateWriterMutex或调用RTS/USER32。字段分别读取时说明是观测快照，不假装所有线程原子事务。 |
| `Draw3.Host.cpp` | 在现有 `RuntimeSnapshot()` 调用链中、关键业务锁之外，按Cursor开关和原子限频每秒最多一次采样/入队 `rts-probe`，即使RTS不再callback也能报告。另带现有workspace/selection/commandScenePending/admissionRevision及 `occupiedSlots/slotCapacity`，复用已有clearCommandCount/undoCommandCount等已处理计数核对74秒反证；可计数项必须附enabled标志/基线。正常StateMonitoring已调用快照，不另发wake。 |
| `Draw3.DrawingController.cpp` | 只在Down dequeue和三处准入拒收分支补一条有界 `contact-ingress` 事件，带tcid/cid/record-generation、Down/current admission revision、workspace和明确reason；gate通过与initializeStroke返回值分别命名，不能把init返回true（也可能是gesture处理）一概称为stroke已建。不要为日志新增或传播产品命令序号字段。 |

日志最少需支持四种可判别结果：

1. `observed-enabled=false`：最后通知是Disabled，检查谁触发及是否有后续Enabled。它仍不是实际COM属性回读。
2. observed-enabled=true，writer owner/阶段长时间停在同一API：优先抓该RTS/调用线程栈；不是collector完全未进入callback。
3. Down持续arrival/no-decoder，writer正常返回：查看local-lifecycle、scale、requested/first tcid和HRESULT；collector至少仍交付Down。
4. Down持续published，controller `gate=drop`：看具体admission/场景原因；若gate-pass而仍无笔迹，继续runtime/model/raster层，不再称为RTS lost。

若observed-enabled=true、writer idle、decoder镜像有效，却无Down arrival，而Win32鼠标接触和probe都持续，才将“collector交付/路由问题”升高；它仍可能是实际COM属性/回调线程内部状态变化，需要下一步证明。

该最小方案的输出仍由既有绘制线程FlushCursorDiagnostics排空，符合视频中绘制帧继续推进的前提。若故障发展成绘制线程也停止，probe可留在内存队列但不保证落盘；此时另用外部dump或主会话已有异步文件logger做独立低频输出，不把“末屏无probe”误判为StateMonitoring也停。

#### 8.3 实际COM Enabled与collector的最后分界

上述A方案刻意不向RTS新增外部调用，无法把lifecycle通知镜像提升为实际 `get_Enabled()`。若必须确认实际属性，另增加一次受控、只读的get_Enabled/get_HWND采样，运行在创建RTS的既有MTA所有者路径，记录API进入/返回/HRESULT/时长；不要在UI/RTS callback/持writer期间同步请求，也不要为此新绑第二套RTS、反复Disable/Enable或改HWND。COM读取可能阻塞/扰动时序，不应把它混进常规heartbeat，也不能用不安全的线程终止来做超时。缺乏安全所有者调度时先取外部线程栈，明确“实际COM Enabled未知”。

无行为变更的审查门：只添加有界观测、必要原子及诊断时钟读取，原有输入/窗口/COM业务调用次数和顺序保持，writer/readers协议保持，诊断关闭时不做外部查询/格式化、不改变任何contact准入或唤醒；诊断开启也不做恢复。验证应覆盖开关关闭、开启正常操作、Down三类分层日志和静态检查，新增fake事件只能验证诊断字段，不能证明真实RTS系统触发。

用同一HEAD二进制先跑原操作链，再把“设置保持打开时切工具”“隐藏设置后切选择/画笔”“显示DPI/方向真实变化后重新落笔”当独立触发条件。真实显示设置变化影响用户桌面，本研究不执行。CPU级stub重入测试只能验证代码在**假设重入**下的表现，不代替现场证明。

### 9. Related specs

- `.trellis/spec/native-desktop/input-and-ink.md`：RTS唯一producer、cursor与contact分层、callback非阻塞日志、contact终态隔离。
- `.trellis/spec/native-desktop/draw3-integration.md`：唯一Host、Window Service HWND所有权、输入/渲染线程和重建边界。
- `.trellis/spec/native/runtime-and-rendering.md`：RTS同步回调和绘制线程边界、cursor mailbox/唤醒。
- `.trellis/spec/native-desktop/cpp-conventions.md`：改变锁范围/在锁内新增COM必须专门验证。

## Caveats / Not Found

- 静态检查未发现足以直接实施的确定RTS修复；不要以推测性重启RTS、改recursive mutex、取消reader/writer gate或重置活route代替定位。
- 尚无故障现场线程栈、lifecycle entry/exit、实际COM Enabled或callback计数，无法判断“collector不投递”“writer查询卡住”“decoder持续补建失败”三者中的哪一个。
- 公共文档支持跨线程owner会附加input queue，未证明这种变化必然使RTS鼠标输入失效。
- 所有历史结论来自主会话提供的Git输出；本代理遵守research角色禁Git约束。视频EXE身份、完整console原始流与HEAD构建结果仍需主会话确认。
- 新文档的外部技术事实仅采用微软官方来源，未把搜索结果中的第三方经验当成产品根因。

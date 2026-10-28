# Research: 白板退出的窗口模式、层级与恢复链

- Query: 退出后背景/分页已隐藏，主栏未归位且层级/任务栏跳变；点击画出墨迹后恢复，窗口 owner 一侧有哪些确定的剩余故障链？
- Scope: internal；仅源码、任务规范、已有报告和主会话导出的只读历史文件；未执行 Git、GUI、进程/窗口/PDB 采样、编译或测试。
- Date: 2026-10-27
- Baseline: 用户已确认并提交的 `ab79dcb9`；本报告不修改产品。

## Findings

### 文件与相关规范

| 文件 | 用途 |
| --- | --- |
| `Inkeys/Inkeys/Window/Window.cpp` | 模式命令、完整/纯Z恢复、owner消息循环、原生激活样式、Shell发布 |
| `Inkeys/Inkeys/Window/Window.cppm` | Freeze任务栏锚点及Drawpad激活样式合同、公开Leave接口 |
| `Inkeys/Inkeys/Window/Window.ZOrderPolicy.h` | strict continuous默认谓词与白板/纯Z相对收敛策略 |
| `Inkeys/Inkeys/Window/Window.CommandCompletion.h` | 在途过期、失败回执和Retired语义 |
| `Inkeys/Inkeys/Window/Window.ShellFullscreen.h` | 三目标绝对状态、同批版本和异步COM调用 |
| `Inkeys/IdtState.cpp:2842–2879` | 仅核对窗口离场调用顺序及Bar收尾依赖；完整状态/输入调查由另一研究者负责 |
| `InkeysHeadlessTests/window_tests.cpp:1643–1659,1777–1851` | 真实窗口fixture的policy/显隐/退出顺序差异 |
| `Build/WhiteboardExitInvestigation20261027/history/{before-ab79,release-20261021a}/Window.cpp` | 主会话导出的提交前/发布基线；本研究者未执行Git |
| `research/history-native-order.md`、`history-window-recovery.md` | 先前已交叉核对的77f76038历史归因 |
| `.trellis/spec/native-desktop/index.md`、`canvas-freeze-boundaries.md:262–307`、`whiteboard.md:25–29` | owner线程、root band、完整模式事务/纯Z边界、最终Desktop归位要求 |

下列行号均为本轮读取的当前工作树，后续修改会移动。

### 1. 高相关剩余硬门：退出先变成非置顶Desktop，再要求全组全局TOP成功

确定的调用链为：

1. `Service::LeaveWhiteboardWindowMode` 在 Window.cpp:3750–3752 转成 `SetWhiteboardMode(false)`，后者在580–587创建Freeze角色的 `SetWhiteboardWindowMode` 命令。
2. `Execute` 的2866–2867仍对进入和退出共同执行 `ApplyWhiteboardWindowMode(target) && RefreshOverlayGroup(&command)`，必须两个均成功。
3. `ApplyWhiteboardWindowMode` 在2269–2286先应用CanvasPolicy/激活样式，随后在2283存储 `whiteboardWindowMode_=false`。这发生在刷新之前。
4. `RefreshOverlayGroup` 的1978–1984只有**当前mode仍为true**时使用新相对排序。因此退出刷新不会走刚修复的白板分支，而是走2003–2043的普通Desktop/PPT分支：根切band、逐个成员（包含隐藏PPT）`HWND_TOP`，最后 `GroupMatches`。
5. `GroupMatches` 在1897调用 `ZOrderSnapshot::Matches` 的默认 strict continuous，见Window.ZOrderPolicy.h:29–39；同时1898–1899要求Bar高于四个PPT，2037–2041还要求原PPT子序列。它仍把外部连续和全局置前结果作为退出必要条件。
6. 生产离场的顺序在 IdtState.cpp:2859–2862：先 `SetOverlayFullscreen(false)`，再 `LeaveWhiteboardWindowMode`，最后才 `SetOverlayTopmost(true)`。白板期间显式topmost政策为false，所以正常生产退出有一段 **mode=false且overlayTopmost=false** 的全组全局TOP路径；后面原本应该恢复的true政策还未执行。

这是可以直接从源码确定的剩余风险，不需要假设本次已经捕获了某个USER32失败返回。它可在后台排序、隐藏PPT/Bar内部序或外部穿插不满足时拒绝退出，与上一轮“相对排序只用于mode=true”的收窄边界一致。

### 2. mode命令不属于Z-only，排序失败仍重建完整白板样式

`IsZOrderOnlyCommand`（2535–2538）仅列Refresh/Promote/SetTopmost/SetFullscreen，**不含SetWhiteboardWindowMode**。因此：

- Capture在2550将mode命令记为group，2554同时记为canvas，并于2577捕获整组几何/显隐/样式。
- 执行失败后 `ExecuteTracked:2818–2835` 保存唯一恢复闭包并立即重试。
- `RestoreCommandSnapshot:2612–2618`恢复请求前政策和 `mode=true`，2640–2647对快照做FRAMECHANGED并强制 `EnsureCanvasPolicy(true,true)`，2649再刷新组。
- `EnsureCanvasPolicy` 的2158–2165再次调用激活样式切换；`ApplyWhiteboardActivationStyle:2371–2389`会Hide、重写受控ex-style、FRAMECHANGED并恢复显隐。
- `Window.cppm:90–99`清楚表明该切换会把Freeze从 TOOLWINDOW|NOACTIVATE 改回唯一 APPWINDOW任务栏锚点，Drawpad也改回可激活。故重复失败/回滚能够反复修改任务栏身份和原生层级，**即使白板背景与分页已被更上层逻辑隐藏**。
- 当前回滚因mode=true会走已修复相对排序，可能很快完成；State随后再次尝试Leave。这里更像“不断重做离场+成功回滚”，不一定是一个长期不退的Recovery。若某次真实恢复本身失败，消息循环1075–1080/1116–1120以250ms重试，普通命令在2792被挡住，直到真实恢复成功（2667–2680）。

因此本次调查不能只检查四类纯Z恢复；上一轮修复刻意保留的完整mode事务在退出方向仍可能放大一个层级后置失败。

### 3. 任务栏跳变不要直接写成Shell TRUE/FALSE振荡

一个必须保留的区分：正常离场在IdtState:2859已经成功提交fullscreen=false，才会执行Leave。因此这次Leave快照的 `fullscreen` 正常应为false，完整回滚2615也恢复false。不能单凭用户“任务栏跳变”声称MarkFullscreenWindow不断TRUE/FALSE。

当前更直接的源码解释是Freeze APPWINDOW/TOOLWINDOW身份与Hide/FRAMECHANGED、全组排序反复切换。`ApplyOverlayFullscreen:3505–3514`另外根据模式、可见性、最小化和SafetyHidden发布绝对值；Hide/Show分别在3001/2982触发它。ShellFullscreen.h:65–74等值去重、:103–109迟到结果不能确认新版本；真实COM只在164–170串行执行。需要区别Shell的重复FALSE重申、窗口任务栏样式变化和实际TRUE/FALSE变化。

若2859本身失败，它属于Z-only，会从2593–2601恢复旧fullscreen政策和物理顺序，且不碰Canvas。此时窗口已经进入前一阶段隐藏状态，具体最终MarkFullscreen仍受3509–3512的可见性检查；本报告没有本次现场数据来认定发生的是哪一支。

### 4. 为什么点击后可能恢复：窗口侧只支持候选机制

[Microsoft SetWindowPos文档](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos)说明全局置前涉及前台权限；[Window Features](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#foreground-and-background-windows)列出最近输入和前台进程的关系。点击Drawpad可能使应用接收最近输入/取得前台，从而让下一次全组置前与后置核验成功。

但它是**推断**：本次没有记录最初失败stage、每次API BOOL、GroupMatches具体失败项或点击前后实际前台，也没有证明首次点击失败时应用此前没有许可。点击也会提交新墨迹、触发Draw3 Present和runtime wake；是否停在Ready门，必须结合状态/输入研究区分。不能把上一次首开/后台闪烁现场的排序失败直接当成本次退出现场证据。

### 5. 历史判断：旧链仍在，不等于ab79新引入

主会话导出历史可直接核对：

- 20261021a 的Window.cpp:1973–1974只返回 `ApplyWhiteboardWindowMode(...)`；其普通/白板非置顶刷新在2359–2374只尝试Bar TOP并读回root band，没有当前完整group连续性硬门。
- ab79父版Window.cpp:2710–2711已是 `ApplyWhiteboardWindowMode(...) && RefreshOverlayGroup(...)`；其mode组canvas快照在2419–2424，完整恢复刷新在2493。
- 当前ab79只给 `mode=true` 刷新增加相对收敛，并为四类Z-only早分流；mode退出的普通排序硬门和完整恢复仍保留。先前history-native-order/history-window-recovery已经独立确认这些硬门/通用恢复首次来自77f76038。

所以“新观察到退出异常”不足以断定ab79新写坏退出分支。更准确的判断是：原77链在退出方向仍存在；上次进入/后台修复没有覆盖该方向。是否ab79改变了触发概率或只让用户能够顺利走到此前受阻的退出，需要额外事实，不能仅按提交相邻推断。

### 6. 已有测试为何不能排除此路径

真实Window fixture：

- `window_tests.cpp:1657–1659`先把overlayTopmost恢复为true。
- 1777–1783先Show所有组成员，四个PPT并非生产中的隐藏组合。
- 1846先最小化，1847直接Leave；1848–1851验证离开后完整topmost树。

它没有执行生产2859–2862的“fullscreen=false→在overlayTopmost=false下Leave→true”顺序，也未注入后台Bar全局TOP受限。并且这些真实WindowTests本轮按用户限制根本未运行；`--no-window`仅执行新的生产相对helper和纯合同，不能动态穿过完整私有mode(false)分派/恢复。

### 7. 下一轮最小修复边界建议（本报告未实施）

优先把**退出必须满足的物理合同**与**普通Desktop周期置前政策**分离，沿用已存在生产相对helper、真实owner/band/代际/内部序读回，而不把所有Desktop/PPT正常刷新都放宽。

默认最小方案应保留现有生产顺序：fullscreen=false→Leave mode→SetTopmost(true)→ClickThrough；rendering-and-ui.md:1644已明确Leave后再恢复topmost的合同。只分离这段模式/政策物理交接与普通全局刷新硬门：mode(false)事务按真实目标owner/band/内部序确认模式与APPWINDOW切换，后续政策设置及重入同样按必要物理合同确认，不能让Desktop全局TOP/continuous再次否决已完成交接。真正原生样式/region/band失败仍回滚，不能吞掉false或跳过必要读回。完整group回滚也按捕获身份、原PPT序与目标band完成必要物理恢复；普通Desktop/PPT周期Refresh政策可保留，但与离场完成条件分开。调整为先topmost=true再Leave会偏离既有合同，尚未论证，本报告不采用或推荐该逆序。

不能只把 `RefreshOverlayGroup` 的Whiteboard条件改为 `true || WhiteboardMode`，也不宜仅删除mode命令最后的Refresh、提前写Inactive、以超时丢弃Recovery，或仅改变一个SetTopmost调用顺序而继续依赖未验证的全局TOP。完整模式Canvas事务的真实恢复、后到安全Hide/穿透、新代际与取消保护仍须保留。输入/工具准入的先后由另一个研究报告给出最小边界，不能靠点击提供正常唤醒。

#### 7.1 必须覆盖后半失败后的整段重入，不能只修第一次Leave

即便第一次mode(false)已用内部相对策略成功，若后续SetTopmost(true)或ClickThrough失败，State仍会从2859开始整段重试。此时mode已经false：

- 重试的 `SetOverlayFullscreen(false)` 即使政策已经false，Execute:2873–2876仍无条件调用普通Refresh，会重新进入Desktop全局TOP/continuous门。
- 随后的 `SetOverlayTopmost(true)` 即使政策已true，也一样在2868–2871无条件走该门。
- 已经false的mode重试也不能重新附带全局门；否则从入口条件看似幂等，实际仍可制造一次新的完整Canvas恢复。
- 一旦完整mode回滚的快照为mode=false，当前Restore:2649也会走Desktop全局门。只让正向Leave走内部收敛，而回滚仍要求全局TOP，仍可能把物理已恢复的事务永久留在屏障内。

所以稳妥最小合同应覆盖**整段退出窗口交接及其重复调用**：fullscreen/topmost政策、mode/APPWINDOW/Canvas物理切换、最终ClickThrough确认，均以目标政策+真实内部物理关系完成，不依赖常规全局置前许可。普通Desktop/PPT显式周期Refresh仍可维持既有策略，但应与退出完成条件分开。完整group回滚也应按其捕获身份/原PPT序和目标band验证必要物理状态，不能再调用含全局连续硬门的常规Refresh作为恢复终点。

这不要求把所有恢复降成空操作或把一般Desktop行为改成白板；可由既有私有命令/恢复路径明确区分“模式/政策物理交接”与“常规全局刷新”。只改一个if或调换单次SetTopmost顺序不足以证明整段可重复收敛。

建议的离屏验证必须接到**实际mode命令/退出策略**，不只重新测试已有相对排序算法：

1. 生产返回顺序：背景/PPT已隐藏、mode=true、overlayTopmost=false、fullscreen=false；外部应用前台/外部穿插、四隐藏PPT。模拟全局TOP不生效，内部物理恢复合法时退出必须能完成，而Bar不需作为第一笔输入触发器。
2. 真owner/band/activation-style/CanvasPolicy/读回失败：mode退出仍明确失败并持有/恢复真实责任，新epoch和后到Hide/穿透不被旧写回覆盖。
3. mode退出排序故障不得重复切APPWINDOW来取得前台；政策切换失败不得留下错误effective band；快速关闭/重入保留身份。分别注入Leave成功后SetTopmost失败、ClickThrough失败及下一轮fullscreen已经false，证明整段重入不会再次碰全局门；另测mode=false快照的真实完整回滚。
4. 最终私有dispatch和回滚写集必须静态核对；若没有不创建HWND的真实Service注入接缝，不得用单独镜像状态机声称已动态验证整个窗口事务。GUI/各系统结论继续留空。

## Caveats / Not Found

- 本报告确认的是可直接触达的剩余退出硬门和失败放大机制，尚没有此次真实失败位点，不能下结论一定停在Leave而不是之前的Draw3 ready/背景ACK或SetFullscreen。
- APPWINDOW/taskbar身份切换机制是确定源码事实；实际任务栏每次跳变由哪个原生消息/Explorer合成触发未采集。
- 用户返回Desktop还是PPT仍待主会话澄清；两者都会经过该公共窗口恢复链，但Readiness/布局目标不同。
- 未新建通用框架、未修改代码/规范/项目/CSO、未执行测试或Git。只新增本报告。

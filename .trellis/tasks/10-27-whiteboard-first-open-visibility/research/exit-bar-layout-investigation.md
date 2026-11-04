# Research: 白板退出后的主栏布局与工具显示交接

- Query: 白板背景/分页已消失时，主栏为什么仍停留白板位置；点击并落笔后主栏恢复及显示选择，是否来自 Bar 自身动画、缺唤醒或工具自动切换。
- Scope: internal；只读当前源码及主会话导出的既有历史文件，不进行 Git 操作、GUI、运行时窗口/进程/PDB采样、编译或测试。
- Date: 2026-10-27
- Active task: `.trellis/tasks/10-27-whiteboard-first-open-visibility`。

## Findings

### 1. 文件与接口

| 文件 | 用途 |
| --- | --- |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp` | 主栏独立 whiteboardActive/PPT状态、业务状态更新与 render唤醒。 |
| `Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h` | 生产 BarWorkspaceSceneState、场景边沿和底部5DIP合同。 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | 桌面几何基线捕获、恢复、场景切换与任务栏线动画。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp` | 当前工具/内容/白板状态到按钮外观的投影，不是工具权威写者。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp` | AcceptedStroke、DrawingStarted通知、自动收起与辅助面板清理。 |
| `Inkeys/Inkeys/UI/Whiteboard/Whiteboard.cpp` | 与主栏不同的背景/分页 active 和 expandedLayoutTarget。 |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cppm` | 分页工作区可见性独立选择。 |
| `Inkeys/IdtState.cpp` | 仅核对白板退出对以上接口的发布先后，深层状态/Draw3准入由另一研究负责。 |
| `InkeysHeadlessTests/ppt_session_tests.cpp` | 已有生产场景状态机的离屏用例范围。 |
| `Build/WhiteboardExitInvestigation20261027/history/before-ab79/IdtState.cpp` 和 `before-ab79-to-ab79.diff` | 主会话已导出的历史材料；用于核对本次提交有没有直接改写正常退出尾段。 |

以下行号以研究时冻结的当前源码为准。

### 2. 确定事实：分页隐藏与主栏退出不是同一个状态位

| 发布 | 消费者和结果 | 退出中的时机 |
| --- | --- | --- |
| `Whiteboard::PublishExpandedLayoutTarget(false)` | PageControl bottom离开WhiteboardExpanded；没有有效PPT时目标Hidden | `IdtState.cpp:2727`，Active刚开始退出即发布 |
| `Whiteboard::PublishActive(false)` | 白板背景及PageControl的active关闭 | `HideWhiteboardForReturn:193–200`，Active退出早期和后段均调用 |
| `Bar::SetWhiteboardActive(false)` | 主栏工作区由Whiteboard切换到Desktop或Presentation，并发布渲染请求 | `IdtState.cpp:2874`，所有后段窗口/穿透事务成功后才发布 |

`Whiteboard.cpp:81–106`把自己的active和expandedLayoutTarget传给PageControl；`:335–348`两个Publish分别更新自己的原子值并请求相关客户端。`PageControl.cppm:936–951`依据expandedLayoutTarget和PPT场次事实选WhiteboardExpanded/PptCompact/Hidden，完全不查询`Bar::WhiteboardActive()`。

因此“白板背景与分页已消失、主栏仍白板位置”能由正常分步退出的中间态直接产生。若后段门一直失败，中间态可长期保留；它本身不证明Bar丢了一帧或上移动画卡死。

### 3. 主栏归位的实际必要条件

`Bar.Main.cpp:503–521`维护主栏独立原子whiteboardActive。关闭时清whiteboardDockLockActive并调用`barUISet.UpdateRendering()`；`:285–318`的UpdateRendering最终同时唤醒Bar计算并请求RenderPipeline::Bar。因此正常接口没有“必须再点屏幕一次才请求退出布局”的缺唤醒。

`Bar.BottomDock.h:64–77`的生产状态机：

- `(whiteboard=true, presentation任意)`仍为Whiteboard；同态Update返回Keep。
- `Whiteboard→Desktop`即`(false,false)`返回Leave。
- `Whiteboard→Presentation`即`(false,true)`返回Switch，保留第一次进入覆盖工作区前的Desktop基线，不能此时恢复Desktop位置。

`Bar.RenderLoop.cpp:12905–12916`仅在起始布局完成、没有真实拖动、directWindowDragPhase可以取得Absorbing时消费场景。进入新覆盖工作区在`:12923–12934`保存desktopDockMode/centerMode/fold/方向/屏幕XY/monitor。Leave时`:12937–12953`恢复这些模式并标记desktopRestorePending；`:13057–13073`按当前可见工作区钳制保存XY并直接写回布局坐标，清pending。它不是依赖鼠标输入推进的独立“白板退出上移动画”。

`IdtState.cpp:2857–2874`先串行要求SetOverlayFullscreen(false)、LeaveWhiteboardWindowMode、SetOverlayTopmost(true)、必要SyncDrawpadClickThrough均成功，任何false在`:2872`continue；只有随后才关主栏白板标记。故只要这些门仍有一项持续失败，主栏每次读到true，workspaceScene.Update仍Keep，根本不会发出Desktop Leave恢复。

相关真正失败项、是否原生排序/恢复屏障或Draw3就绪，是另外两个研究的责任；本报告不从Bar症状反推已捕获某个API失败。

### 4. 主栏任务栏线策略进一步支持检查上游事务

`Bar.Main.cpp:451–455`的SceneBottomDockInsetDip消费主栏白板标记、PPT场景和Window组最小化。`Bar.BottomDock.h:58–62`使未折叠、未最小化的白板始终使用5DIP屏幕底边，而不是Desktop任务栏线。

`Bar.RenderLoop.cpp:1371–1380`只有Desktop dock才允许任务栏Y动画；`:1390`附近非Desktop会清任务栏retarget。主栏白板标记仍true时，保持白板底部位置符合当前代码。不能因为任务栏本身在抖动就把故障定位到Bar Y插值或额外优化该动画；Window mode/band/fullscreen的反复变化应先由原生研究核对。

### 5. 点击产生的Bar副作用没有Selection写入

- `IdtMain.cpp:3521–3542`的Host回调将实际DrawingStarted/Ended、AcceptedStroke通知Bar。
- `Bar.Interaction.cpp:9518–9543`分别入有界notice队列、唤醒interaction worker，或给Bar owner发绘制活动消息。
- AcceptedStroke实际消费在`:905–924`，仅在场景身份当前且配置允许时调用FoldBarAutomatically。`:828–838`将fold=true，清辅助面板和dock lock并请求渲染，不更换StateMode。
- `:8462–8475`的HandleCanvasDrawingActivity只清Recall锁存、收辅助面板、UpdateRendering(false)、休眠边框光标，不切选择。
- `Bar.Main.cpp:358–363`的SetCurrentPageHasContent只更新内容bool并重算UI；`Bar.RenderLoop.cpp:1170–1178`每帧读权威工具快照，`Bar.Button.cpp:960–1015`由此显示Select/Pen/Pierce。compactSelection还要求非白板、Selection和空内容。

因此点击可以额外唤醒UI、更新内容态，并按配置自动折叠主栏；不能只按视觉“恢复”推断Exiting已经结束。但该链没有“落笔后自动ChangeStateModeToSelection”的规则。真正Selection发布或旧工具与绘制线程交接须由状态/输入研究解释。

另一个研究已指出SwitchWorkspaceStateMode会在退出早期恢复目标工具，本报告不将工具恢复一概描述为“延后到Bar关闭之后”。主栏标记/几何晚发布与工具早发布是不同事实。

### 6. 与ab79dcb9的关系

主会话提供的提交范围确认没有改Bar/UI/PageControl/Business产品文件。导出的`before-ab79/IdtState.cpp`中正常退出尾段已有同一串行presentationWindowStateReady门和Bar.SetWhiteboardActive(false)的晚发布，`before-ab79-to-ab79.diff`也没有修改该正常尾段。新增进入准备去重主要覆盖进入/反向进入；它没有直接重写Desktop恢复算法。

所以当前证据不支持“刚提交补丁改坏了Bar自身上移动画”的结论。更符合代码结构的候选是：之前进入/闪烁问题修复后，原来退出门或两条状态发布不同步暴露为稳定可观察中间态。具体历史根因和触发点仍须原生及状态研究会合，不能仅凭未改Bar文件排除跨模块回归。

### 7. 修复和离屏验证建议

1. 首先确认退出后段哪一项返回false；若窗口模式/层级恢复未完成，应修该事务的物理合同和责任，不直接提前Bar::SetWhiteboardActive(false)掩盖故障。提前解除Bar白板状态会改变PPT业务focus、Freeze可用性、dock基线和快速重入，需完整事务设计。
2. 维持Desktop Leave与返PPT Switch的不同语义。已有`InkeysHeadlessTests/ppt_session_tests.cpp:390–407`直接测试生产BarWorkspaceSceneState与baseline规则，但没有模拟IdtState退出中某窗口事务连续失败，不能拿既有通过作为这次故障已覆盖。
3. 新的离屏回归应把退出阶段与UI发布接缝串起来：先Publish白板分页隐藏、原生门失败多轮，断言主栏标记和Desktop基线仍为原事实；资源健康后完成最终发布，自动产生一次Leave/恢复请求，无需AcceptedStroke；返PPT必须Switch不覆写Desktop基线。
4. 若能确认主栏原子已false、PPT也无会话而仍不归位，再检查RenderLoop:12905的drag/CAS门和最终ULW失败续帧；目前没有这组现场证据，不应先改它们。

## Related Specs

- `.trellis/spec/native-desktop/whiteboard.md`：“场景与底栏”要求第一次覆盖工作区记录Desktop基线、PPT/Whiteboard互切不重拍、最后返Desktop恢复；GUI仅获授权后可执行。
- `.trellis/spec/native-desktop/rendering-and-ui.md:1644`要求退出恢复DrawpadPresentation穿透、释放白板dock lock、LeaveWindowMode后再SetOverlayTopmost(true)，不能为了Bar症状擅自反转顺序。
- `.trellis/spec/native-desktop/index.md`要求区分UI/Window/Draw3所有权与实际证据边界。

## External References

本报告仅内部源码与主会话导出历史文件，不涉及外部API语义判断，无外部参考。没有调用Git命令。

## Caveats / Not Found

- 没有本次退出故障的运行时阶段、前台、回执或原生BOOL；旧首开/闪烁现场不能当作此次退出现场。
- 用户返桌面还是仍有有效PPT会话在本研究完成时尚未由主会话告知，报告分别列两条语义。
- 没有创建/操作GUI、采样进程、编译、执行测试或修改产品/spec/其他研究文件。研究输出不宣称修复或实机通过。

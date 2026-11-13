# Research: 多设备漫游全部失效的现场日志与DM输入归属

- Query: 用户反馈上一版本全部设备不能拖动，仅双指触控板偶尔可用；定位真实输入是否到达导航，并审查新增DM是否抢占原RTS/鼠标输入。
- Scope: mixed（附件日志、当前修复前源码、Microsoft官方文档及上游primary implementation；产品只读）
- Date: 2026-11-08
- Attachment: `C:/Users/alan-/.codex/attachments/3244ff61-0772-49b3-af29-e52e9e8162c8/已粘贴的文本.txt`

## Findings

### 1. 现场日志明确显示：已呈现的运动完全来自native流

附件UTF8文本816226字节、2692行。开头已截断，第一条完整CanvasPan记录为seq328；末条CanvasPan为seq3011。计数：

| 记录 | 数量 | 观察 |
| --- | ---: | --- |
| render | 911 | 可正常恢复tile、合成并Present |
| touchpad | 872 | 全部epoch9；Running=562、Inertia=301、Ready=9、Cancelled=0 |
| pan-step | 893 | **全部phase=Idle、held=0、samples=0**；部分有delta，来自native累计变换 |
| navigation-stop | 1 | seq3011 leave-move-mode |
| pan-register | 0 | 看不到任何成功准入的Mouse/Touch/Pen/PenTail Down |
| input ordinal= | 0 | 看不到任何已登记接触事件消费 |
| rts-batch | 0 | 对非Pen批次亦无记录，但受后述条件限制 |
| history-gap | 0 | 没有现成的ContactInput导航历史溢出证据 |

所有完整CanvasPan seq范围的差额是8条，日志有7条DiagnosticOutput drop统计且最终dropped=8。少量诊断输出丢失不能合理解释整个片段接触型导航一直为零。原始QPC跨度134457619 ticks；附件没有frequency头，不单凭本文件把它表述为硬件秒数。

直接锚点：

- 附件line3/seq329：native Inertia、epoch9、sequence99，正常改变视口；line4/seq330的pan-step仍Idle/held0/samples0。
- line19/seq345：同epoch9从Inertia切回Running，累计坐标保持连续；line98/seq424再进入Inertia，line161/seq487才Ready。
- 还有多次同epoch Inertia→Running不经Ready，如line679/seq1005、line1013/seq1339；这与运行中DM可自动关联新接触的API行为相容，但日志没有pointerId/type，不能逐段认定实际是鼠标、触摸或笔。
- line2682/seq3008：最后一条native Ready；line2688/seq3011退出移动模式。整个片段没有native epoch因真实新来源接手而改变。

软件时间样本：828条非负sample-to-submit，median8.819ms/P95 9.944ms/max11.935ms；render present-ms median0.124/max2.328，frame-ms median0.756/max3.816。它们说明已收到的native变换可以正常合成提交，不支持把“全部不能拖动”的首要原因归结为绘制长阻塞。Present返回仍不是光学显示。

### 2. 从现有诊断可定位的边界与不能定位的边界

- `Draw3.DrawingController.cpp:2872 CanvasPanInput::Register`无条件输出pan-register；0条意味着本片段没有接触进入该已准入集合。
- `Draw3.DrawingController.cpp:3143 HandleSelectionDown`在NativeRunning(atDownQpc)时直接DiscardUntilTerminal；只有获准Pan分支才CancelNative/Register。若RTS/鼠标Down确已进入但被native Running拒收，也会没有pan-register/input ordinal；日志缺少拒收路径的原因记录，不能仅凭这些零值把OS上游和此consumer仲裁完全区分。
- `Draw3.RealtimeStylus.cpp:1454`的rts-batch明确要求`decoder->deviceType != InputDeviceType::Pen`。**不能用0条rts-batch直接断言Pen没有RTS回调**；若当前是Win32Mouse fallback，本来也没有RTS批次。这是研究及最终说明必须保留的限制。
- 渲染日志本身没有输入后端初始化段，也没有DM_POINTERHITTEST pointer type与SetContact结果。根因修复可由源码/API的确定合同缺口支持，但是否现场唯一原因仍需生产接线/用户设备验证。

### 3. 确认的源码合同缺口：手动更新没有隔离输入所有权

阅读时修复前源码：

- `Draw3.WindowControl.cpp:376–377 TouchpadNavigationOwner::Initialize`只设置`MANUALUPDATE | DISABLEPIXELSNAPPING`，未设置`INPUT`、`EXPLICITHITTEST`，也没有独立`SetInputMode`调用。
- `:390–394`将manager激活并Enable viewport；故这个viewport确实参与系统输入命中，而不只是一个被动数学对象。
- `:2033–2061 HandleWindowMessage`仅在CanvasPanMode/allowed/enabled、GetPointerType成功且PT_TOUCHPAD时显式SetContact。拒绝/失败分支返回DefWindowProc；不符合外层条件的DM_POINTERHITTEST也会一路到`:2794 DefWindowProcW`。
- 因此“我们只在type==PT_TOUCHPAD时调用SetContact”不能证明“DM不会占用其它输入”。DM可在app代码之前/默认处理阶段自动hit-test、捕获并消费其它驱动manipulation的contact。

Microsoft文档说明：

- [Viewport options](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/ne-directmanipulation-directmanipulation_viewport_options)：MANUALUPDATE控制更新驱动；INPUT让关联接触的输入到UI线程；EXPLICITHITTEST保证RUNNING/INERTIA时新的WM_POINTERDOWN交app命中，否则DM自行命中且应用看不到。
- [SetInputMode](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationviewport-setinputmode)：默认AUTOMATIC会消费驱动manipulation的输入；MANUAL允许app收到这类输入。SetViewportOptions会覆盖其它选项，设置顺序必须明确。
- [Input mode枚举](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/ne-directmanipulation-directmanipulation_input_mode)：输入自动/手动的意义不同于更新自动/手动；不能混淆SetInputMode与SetUpdateMode。
- [SetContact](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationviewport-setcontact)：关联接触后，DM识别出manipulation即可能捕获该contact并停止向app交付被消费输入。

结论分级：**遗漏输入归属隔离是已证实的源码/API合同缺口；其导致现场鼠标/Touch/笔不能到原输入链，是与日志相符的高置信候选，而不是已运行OS故障注入后的唯一根因证明。**

### 4. 为什么原无窗口测试全PASS

- `Draw3.DrawingController.cpp:6621 CanvasPanProbeRig::CanvasPanProbeRig`只绑定纯window/coordinator，不创建真实HWND。
- native生产probe的典型入口`:7788–7807`直接调用`PublishHiddenTestTouchpadNavigation`；窗口实现`:816`直接向bridge发布数值。这覆盖consumer、阶段、epoch与仲裁，**绕开了COM viewport初始化、系统输入可见性与默认hit-test**。
- `InkeysHeadlessTests/draw3_contact_tests.cpp:2096`等直接构造TouchpadNavigationBridge，同样不能检验系统是否先截断RTS/鼠标输入。
- 原审查沿Chromium/Flutter fake viewport+MANUALUPDATE形状取得了“可输出变换而不接管呈现树”的证据，却将其错误外推为“不会抢占另一个RTS producer”。[Chromium helper](https://chromium.googlesource.com/chromium/src/+/HEAD/content/browser/renderer_host/direct_manipulation_helper_win.cc)及[Flutter自身源码](https://api.flutter.dev/windows-embedder/direct__manipulation_8cc_source.html)都只设MANUALUPDATE；它们的完整输入所有权不同，不能替Inkeys混合RTS/DM场景提供这一保证。

### 5. 最小修复与防回归建议（研究，不实施）

1. 在实际viewport初始化路径建立显式、可验证的输入可见性/命中策略，确保DM只关联确认为PT_TOUCHPAD的contact；非pad、unknown、模式/准入不符及SetContact失败不能再落回一个会自动关联的默认DM命中路径。
2. 区分“Manual update”和“Manual input”，若选择SetInputMode MANUAL须核对PTP后续更新如何送达，不能因修复其它设备而意外切断触控板；按官方定义先固定完整ViewportOptions或调用顺序。
3. 用生产初始化/route helper的记录回放验证真实传给viewport的options及失败传播，并覆盖默认命中拒绝路径；仅新增另一个纯值helper或只再次调用PublishHiddenTestTouchpadNavigation不构成此回归的验证。
4. 必要时以既有CanvasPan gate增加一次Down/命中裁决的有界诊断，含pointer type已知与否、显式关联是否成功/拒绝原因，便于区分OS上游不可见与consumer仲裁。不要逐Move同步输出或另开日志系统。
5. 保留现有几何/惯性/默认开关/hover合同；不通过禁用新设备、恢复hover急停、猜wheel或更换输入后端掩盖回归。

## Files Found / Code Patterns

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp`：DM COM初始化、options、系统命中分流及纯值测试入口；本问题的主要上游边界。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.TouchpadNavigation.h`：阶段和历史仲裁；能够影响Down准入，但不能解释未到达producer的输入。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`：真实contact准入、NativeRunning拒收、生产数值probe的入口。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp`：日志有非Pen过滤条件，解读0 rts-batch时不可越过此限制。
- `InkeysHeadlessTests/draw3_contact_tests.cpp`：bridge生命周期/retirement回归；没有真实OS自动捕获边界。

## Related Specs

- `.trellis/spec/native-desktop/canvas-pan.md`：DM仅触控板、RTS/Mouse/笔继续可达及普通更新不抢占帧节拍的合同。
- `.trellis/spec/native-desktop/input-and-ink.md`：一个绘制producer、RTS/Win32 fallback分工；native导航不能截断已选producer。
- `.trellis/spec/guides/cross-layer-thinking-guide.md`：测试必须跨过真正输入所有权边界，不能用末端发布替代producer可达性。

## Caveats / Not Found

- 本研究没有运行GUI、OS输入注入、COM viewport、编译或测试；没有改变产品代码或任何Git状态。
- 附件从seq328中途开始，输入后端初始化/用户实际设备动作与pointerId未记录；不声称已逐类复现或量得设备物理延迟。
- Web事实来自Microsoft官方；Chromium/Flutter只用于解释此前证据外推盲区，不代表其单独MANUALUPDATE配置本身在各自产品里错误。
- 实现/检查代理需根据修复后的实际生产代码补充验证与规范，不能只把本候选结论当成修复已通过。

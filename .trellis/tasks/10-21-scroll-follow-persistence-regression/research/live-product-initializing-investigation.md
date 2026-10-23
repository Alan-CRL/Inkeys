# Research: 真实产品 Initializing 卡住调查

- Query: PID72368 真实整链夹具停在 Initializing、Capture session0，是否缺 Tick 驱动及最小忠实修复。
- Scope: internal
- Date: 2026-10-22

## Findings

### 实际日志与定位

读取的真实目录为 `Build/ScrollFollow/product-live-72368-213134453/result.log`（派发消息中 `212919656` 不存在）。外层 `live-product-click-unlocked-test.json` 是 exit=-1073741819，即0xC0000005，elapsed12678ms。stderr 仅有 `[ScrollFollow] event=initializing session=1`，没有每秒 stats、没有 initialization-timeout。

初始 pen-ready：capture ready=1、session0、业务Off、projection0、actual_ack1、visible=(1,0)、input_mode3。Toggle后失败快照：业务Initializing/session1、ownerPhase2(Active)、capture仍session0/accepted0/publishing0、projection0。source实际OS激活和99/99屏幕GT已经通过，不是source刷新失败的证据。

### Tick 不需要另一个 UI3 驱动

`Inkeys/Inkeys/Business/ScrollFollow.cpp:240–263` 的 Service构造自动建立唯一jthread；Run在自身mutex下wait_for16ms，之后每次调用Tick。`Get:369` 构造静态Service；RequestToggle置toggle并notify。不存在等UI3调用Tick的产品合同。

Begin→StartCapture精确条件位于约314：state==Initializing、!stop，owner.phase==Active、!captureStarted、now>=nextStart。创建Context时started=now、nextStart=now。owner已经Active而capture0通常应在下一16ms Tick启动Capture；没有每秒stats和5秒timeout，说明控制线程没有正常跑完首次/后续Tick，不能靠再加等待或再启动另一Tick线程修复。

### 最强源码候选：Change 后 UI 刷新依赖尚未建好的 wordMap

- `ScrollFollow.cpp:243–250` Change先写visibleState/session，再Log(event=initializing)，最后RefreshUi。
- `ScrollFollow.cpp:160–167` RefreshUi在普通绑定直接调用 `::barUISet.UpdateRendering()`。
- `Bar.Main.cpp:285–309` UpdateRendering默认updateState=true。StateUpdate缺preset确实早退，但随后**真实工具IdtPen分支无条件调用 ThicknessDisplayUpdate**。
- `Bar.State.cpp:68–73` ThicknessDisplayUpdate使用 `barUISet.wordMap[DrawAttributeBar_ThicknessDisplay]->content.SetTar(tar)`，没有nullptr保护。`Bar.Main.cppm:637` 是shared_ptr map；key尚未存在会插入空shared_ptr。
- `Bar.Initialization.cpp:175/299/844` 的真实 InitializeUI才建立该文字对象；当前 `Draw3.ProductLiveTest.inl:261–430` 没有调用InitializeUI也没有启动Bar初始化。

所以此前 `live-product-test-design.md` 关于“preset早退足以保证Business RefreshUi安全”的结论不完整，本报告更正：**不能继续使用原裸UpdateRendering假设。** 此生命周期不完整解释visibleState已变而Capture/timeout都未推进，并存在明确无效wordMap解引用；AV精确线程/异常栈尚未采集，不能仅凭这条链声称已证明唯一AV指令。

### 最小修复建议：复用现成生产 readiness 刷新入口

优先让 Business::RefreshUi 改用已导出的 `Inkeys::UI::Bar::RefreshToolState()`：

- `Bar.Main.cpp:371–374` 按真实原子contentStateUpdatesReady调用UpdateRendering(false/true)，注释已明确“早期CLI尚未建好预设”。
- 导出 `Bar.Main.cppm:1230`；readiness在Main.cpp:563的 SetContentStateUpdatesReady设置，真实Bar初始化后才为true。
- 早退夹具不建图时仅唤醒渲染请求，真实UI就绪时仍执行原StateUpdate和粗细文字。这不fake ready、不fake ACK、不绕Capture，也不要求第二个Tick。
- 正常Main也是先Host后异步UI3初始化（Main.cpp:3744–3769），Business Get/状态回调有早期访问可能；将裸刷新收敛到已有readiness接口是生产生命周期防护。夹具本身不应为了通过伪造wordMap占位/手写contentStateUpdatesReady。

若坚持不改生产文件，fixture可调用真实 `InitializeUI(barUISet)` 创建图，而不是只make一个ThicknessDisplay对象；但牵涉字体/资源及UI3图生命周期，远大于复用已存在RefreshToolState，而且普通产品早期裸调用缺口仍未解决。完整Bar普通启动不是当前故障的必要修复。

建议重跑前仅加有限观测：记录Change进入/RefreshUi进入返回、首次Tick StartCapture前后。若readiness刷新后仍卡住，凭这些实际进度/线程栈继续定位，不能在无证据时调整Mag等待或StartCapture条件。

### 第二个夹具差异：COM STA 导致 RTS 主动失败

- 真实Main `Inkeys/IdtMain.cpp:2742`：`CoInitializeEx(NULL, COINIT_MULTITHREADED)`。
- 当前 `Draw3.ProductLiveTest.inl:290`：主线程 `COINIT_APARTMENTTHREADED`。
- 实际stderr明确 `[RTS] ... stage=com hr=0x80010106 initialized=0`，即RPC_E_CHANGED_MODE；Host.cpp约2176开始真实stylus.Initialize失败才启用Win32Mouse fallback。

因此本轮input_mode3不是“当前硬件自然不支持RTS”的证据，是夹具先选STA与真实Main MTA不同造成。最小忠实fixture修复是与Main一致MTA；窗口服务仍保留自身原STA线程，不改Mag线程设计。修复后鼠标输入是否能画笔应重新实测，不能保留错误STA去强迫fallback，然后宣称生产真实输入通过。

### 清理与风险

当前inl约504失败后调用RequestStop再等待Off；随后RequestShutdown、offSignal、joinState、StopProduct、StopMag、join、StopAndJoin。若Business控制线程停在无效UI刷新，主线程RequestStop还可能等待该mutex，故超时后清理也可能停顿。应先修生命周期缺口，再收集正常清理事实；本次AV不得记正常释放。

实际状态机并不缺UI3Tick驱动，也不缺StateMonitoring/MagnifierThread的启动调用；当前inl确实起了两线程。仍需检查线程returned/failed字段和必要诊断，不能把仅构造jthread当正常循环证据。已读日志中没有cleanup最终行。

## Files Found

- `Inkeys/Inkeys/Business/ScrollFollow.cpp`：自动worker、Change/RefreshUi、StartCapture及5秒timeout。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.ProductLiveTest.inl`：真实GetService/StateMonitoring/MagThread夹具，未建Bar图，主线程COM模式与Main不同。
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp`、`Bar.Main.cppm`：readiness刷新导出、裸UpdateRendering与图状态。
- `Inkeys/Inkeys/UI/Bar/Bar.State.cpp`：无条件ThicknessDisplay文字map解引用。
- `Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp`：真实UI图和文字对象建立。
- `Inkeys/IdtMain.cpp`：生产MTA与线程启动次序。
- `Build/ScrollFollow/live-product-click-unlocked-test*`、`product-live-72368-213134453/result.log`：实际故障证据，AV/RTS COM失败/Initializing零capture。

## External References

纯源码/现有日志研究，未启动GUI、未build、未读取桌面；没有外部资料或新增API方案。

## Related Specs

`.trellis/spec/native-desktop/scroll-follow-product.md`、`rendering-and-ui.md`、`input-and-ink.md`：原owner/state循环、真实UI readiness、RTS/backend证据等级。

## Caveats / Not Found

- 没有故障线程栈，null wordMap是明确缺口及最强因果候选；不能将精确挂住位置/AV地址标记已证实。
- 当前失败在真实Capture开始之前，不是Mag刷新/PrintWindow不工作证据。
- 最小修复不需要fake状态、投影、窗口ACK、直接Follow Target，也不需要完整普通应用启动。

## readiness 导出链与精确语义补充

Business 已 import Inkeys.UI.Bar；Bar.cppm:4 export import :Main，Bar.Main.cppm:1230 export RefreshToolState，因此不需要增加模块 import。contentStateUpdatesReady 初始false（Main.cpp:47）。RefreshToolState 以 acquire 读取，false 时 UpdateRendering(false)跳过 StateUpdate/ThicknessDisplay，但仍 BarAtomic::wait.Notify 与 RenderPipeline::Request；true则完整原行为。SetContentStateUpdatesReady 在Main.cpp:561保存真实状态，true后立即补一轮完整更新。Initialization.cpp:210在InitializeUI、preset注册/Load/StateUpdate后置true，216/242的失败分支及253退出归false。

普通Main约3550先WindowService和3600后Host，3744–3769才异步Bar/StateMonitoring/Magnifier；IdtState::FollowToolIntentReady:70在ProductRunning时调用Business GetSnapshot构造Service，是早期Service可存在的实际入口。初始Service Off不必刷新，但早退夹具RequestToggle触发Change是已实测到的无UI图实际路径。正常UI按钮本身须图建好后才能点，所以不能夸大声称普通启动已复现同AV；此修复统一了Business的UI生命周期门。

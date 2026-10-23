# 真实产品最小实屏夹具：离线准备、接线与构建

2026-10-22，`feature/move / 10abcac3` 后的共享工作树。本轮先按父会话要求仅在 ignored `Build/ScrollFollow/product-live-stage/` 准备；随后父会话明确交接了 Main/header/include 接线与串行完整 ARM64 构建。当前源码已冻结，后续交独立离线 check。本任务继续 `in_progress`，没有 commit/push、Computer Use 或子代理。

**当前完成的是可编译、可严格早退的真实产品夹具，尚未运行精确 live 参数。** 用户尚未再次回复人工解锁，父会话仍禁止 GUI、输入、截图及试探安全门。本实现没有运行窗口、OS 输入、截图或正常 Main，也没有启动 Edge/Word。正式 Capture 的 20 次循环/Freeze 扩展由另一实现者完成并已离线 check，实际门槛仍待父会话接管；不能把本报告的 build 或纯 CLI 当作实屏通过。

## 最小文件接法

已接入四个文件；没有项目/SDK/依赖/生产 API、Capture/Tracker 算法或输入后端修改。

| 文件 | 本轮增量 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.ProductLiveTest.inl` | 新 harness 夹具，462 行，真实空页 Tracking → 一次 OS wheel → Off |
| `Inkeys/IdtMagnification.cpp` | 现有 harness include 块中加入 Product/Business/IdtState 三个头；TU 末尾 include 新 inl。三个头必须在 `import Inkeys.Window` 前，避免共同退出凭据重定义 |
| `Inkeys/IdtMagnification.h` | 仅内部专测声明 `int RunScrollFollowProductLiveTest();` |
| `Inkeys/IdtMain.cpp` | 原最早 whole-argv flags 表追加第六项 `--scroll-follow-live-test`、循环上限 5→6、宏内分派一行 |

新 inl 编译在既有 `IdtMagnification.cpp`，只 include，不增加 vcxproj/.filters 或 Headless 入口。harness-off 的 inl 实现仅返回 2；Main 原 whole-argv 扫描仍在配置、单实例、Display、普通窗口和正常启动之前，识别完整前缀但只接受精确单参数。错位、后缀、多余、重复和混合 flag 都必须早退 2。

复用了同 TU 私有 `LiveSecurityInterfacePresent`、`LiveDesktopAvailable`、`BuildCurrentFilterList`，不依赖已撤销的 owner probe、raw 驻留、私有 owner RPC 或诊断消息。原 `PrepareMagnifierWindow/MagnifierHostCreated/MagnifierChildCreated/ShutdownMagnifierWindow` 和原 `MagnifierThread`、readyPromise/owner 生命周期保持不变。`verify_applied.py` 从最终 Mag.cpp 移除本轮三个 include 和末尾 include 后，恢复的 SHA256 精确为捕获冻结版 `512a650ea9007c9cc5080b51cd0d4a9af4f02c04e05c6d60b13d29758f147dd0`，因此没有改捕获 20 循环正文。

## 真实链与首版证据边界

夹具用 `Window::GetService()` 进程单例，创建原 Mag pair、真实 Drawpad/DrawpadPresentation、隐藏 Freeze/Setting 和真实 DisplayObserver。Drawpad 使用原 `DrawpadMsgCallback` 与 WindowService 样式回调；呈现保持 `Automatic`、默认能力探测/DComp 预配置，不强选 ULW 或鼠标后端。Setting/Freeze 的私有隐藏占位 proc 是 `DefWindowProcW`，本例不进入其 UI；不宣称设置/白板界面通过。

唯一真实 `StartProduct` 附着原双窗，启动真实 `StateMonitoring` 和原 `MagnifierThread`。工具用实际 `ChangeStateModeToPen/SyncDraw3State`，DPI 目标回调复用原 `ApplyPenWidthContext/SyncDraw3State` 规则。控制只调用 `Business::RequestToggle` 和 `RequestStop`，观察公开 Snapshot、真实 Channel、输出 ready、实际 HWND visibility 和只读 `ScrollFollowPresentationAcknowledged`。

没有安装 Business TestPorts，没有直接调用 Capture Start、Tracker Initialize/Process、Follow Begin/Target/End/Handoff/BarrierReady、投影 setter 或写 ACK。真正 Capture callback 由 Business 独占，Frame/Tracker/owner 都走现生产链。诊断复用 Main 唯一 `DiagnosticOutput::Session`，夹具只开启原 Business verbose；不另外开 console。

首版空页有意不画 A，因此真实 selection surface 决策是 **pair=(0,0)**，requested/ready output 仍为 SelectionUlw/full-clean。它只验证空页真实 ACK、真实系统 wheel 路由、可信 Tracker 位移/owner viewport 和 Off 恢复 **pair=(1,0)**。不能将它写成 Presentation 墨迹可见/固定透穿视觉、笔输入、文件保存或完整用户操作已测。父会话已明确接受这个分层。

## 专用内容、真实输入和 GT

- 激活的专用 source 是主屏 popup、同进程但不绑定排除角色。启动核实际角色 filter 构造结果不含 source；没有生产排除白名单例外或新 Mag/WGC。只调整本次 source 和本组窗口的 Z 顺序/前台，不操作外部窗口。
- 虚拟内容来自固定世界坐标散列的 4px 灰度细节，加每 96px 区域不同位置的 L 角点，非周期、非逐帧重新随机。横向 offset 预算 16 屏、纵向 24 屏；初始 `(256,512)`。内容只在 source 自己收到 tagged `WM_MOUSEWHEEL/WM_MOUSEHWHEEL` 后改变，不靠 timer/自造 Follow Target。
- 第一例只用一次 `SendInput(MOUSEEVENTF_WHEEL, -WHEEL_DELTA)`，对应 source offsetY 增加 16px、图像 totalY 预期约 -16px、viewportY 约 baseY+16px。移动光标同样用 OS SendInput；发送前两次核安全门、foreground 精确是 source、中心命中 source，防止把滚轮送给个人窗口。源记录 `GetMessageExtraInfo` 标签、真实消息、delta/坐标、paint sequence、世界 offset、QPC、纹理 hash。没有 SendMessage/PostMessage 冒充 OS 命中。
- 纹理源与控制同 main 线程，所有等待持续 PeekMessage/DispatchMessage 泵送，不用 sleep 阻塞 repaint。外来 wheel、Esc、WM_CLOSE、线程异常会失败并清理。
- 屏幕 GT 必须 99 个专用点全部正确，另在 `(64,64,256,192)` 校验全部 49,152 像素；只有两者通过才保存该专用 ROI PNG。99 点明确只是全屏抽样。截图不是 Mag 图像；本首版无墨迹，不拿它证明实际墨迹稳定。当前未运行此分支，尚未生成这些图片。
- 进入 Tracking 后要求真实 ACK、空页/双隐藏及后续 Capture 样本继续增长；wheel 后等两个不同生产 seq 的 absolute total 与 owner viewport 都在 2px 内并已 Applied，至少增加三个真实 accepted 样本，再 RequestStop。只读 GT 不传给 Tracker/Channel。
- Off 要求 Channel Complete/real handoff、final seq 已 Applied、捕获 occupied=false、原工具 mode revision 和 content revision 不变、真实逆向 ACK 与主窗可见。没有手动解除跟随 gate。

后续第二步可先用真实 OS SendInput Down/Move/Up 尝试 A。当前 Host 正常优先 RTS，只有真实初始化失败才选 Win32Mouse；合成鼠标是否被 RTS 消费必须用实际 input/完成事实确认。若没消费，不能改 inputbackend、伪造 RTS 成功或把鼠标消息算硬件输入；可另开清楚标注的 HiddenTestContact seam 核真实捕获/ACK/可见像素，把真实笔/触控另列待测。首版不为这个限制阻塞 Tracking/wheel/Off。

## 隔离、退出与安全门

只从最早严格 CLI 进入，内存默认配置，不调用用户配置 Read/Write。保存 enable 仅内存设置；Host `autoSaveRoot` 显式指向私有 run root `/AutoSave`，`globalPath` 临时指向同一私有 root 防后续误写落到用户位置。root 为仓库 `Build/ScrollFollow/product-live-PID-tick`，检查仓库标记、每个父目录均为非 reparse，`CreateDirectoryW` create-new，log `CREATE_NEW`。不进入普通 Main 的 startup registry、shortcutAssistant、update、PPT、UI3/global hook/SuperTop 分支。首版无 A，不把 StopProduct 空页路径称为实际 UInk 文件已验。

最前安全门在 Display/COM/source/产品 HWND 前执行；消息等待每轮复查同安全层，screen read 与真实 input 前再核 input desktop。不解锁、不隐藏安全层、不设防睡眠/执行状态，不调用 Computer Use。发现安全层后转单调 Shutdown，不手写 ACK、不以 Default 同名当解除凭据。初始安全层 exit4，运行中被发现也转 exit4；这些分支本轮均未运行。

正常先等真实 Business Off 与反向窗口 ACK，再 `RequestShutdown`，发布原 `offSignal=1` 令真实 StateMonitoring 返回并真 join；`StopProduct` 沿原 final/ExitAutoSave/drain，停止原 MagnifierCoordinator 并真 join 原 MagnifierThread，然后 WindowService 原 owner StopAndJoin/Uninitialize，最后销毁 source、Display Shutdown、COM 和私有日志。状态/Mag wrapper 分别只在原函数真正返回后置 `stateReturned/magReturned`，成功结果必须这些值为真且 Product/Service/Mag ready/Capture occupied 均已停止。没有新增 Business worker join API；其公开 RequestShutdown 单调退出，进程静态 Service 析构仍执行原 jthread 生命周期，不将 Snapshot Off 单独写成其线程已 join。

专用进程自带 90s 最终 watchdog，只能 `TerminateProcess(GetCurrentProcess(),3)`；这代表失败兜底，不能当正常释放或 PW 有界保证。正常/异常清理在 source、回调 context、DIB 和 logger 销毁前先停原 owners。没有按进程名 kill 或清理其它 Inkeys。

## 暂存与精确 apply 记录

`Build/ScrollFollow/product-live-stage/` 保留：

- `Draw3.ProductLiveTest.inl`：与正式目录最终字节相同的暂存源。
- `apply_product_live.py`：默认仅审核并输出 patch，`--apply` 才改四个接线文件，验证已有 5 个严格 flag/唯一锚点和原编码/CRLF/EOF；目标新 inl 已存在时拒绝覆盖。**当前已接入，不要重复 apply。**
- `product-live-apply.patch`：最后由 `verify_applied.py` 按恢复后的捕获冻结基线生成，包含正确的 import 前依赖头顺序；早期 `apply-plan.json` 是首次 apply 历史，不是最终 hash。
- `static-before-apply.json`：冻结捕获基线 SHA/BOM/CRLF/EOF 和首次离线调用检查。
- `verify_applied.py`、`final-offline-static.json`：最终字节、原 Capture 正文恢复证明、最终二进制和实际离线结果路径。
- `run_product_live_offline.ps1`：仅 8 坏 argv + 2 纯 CLI，显式拒绝精确单参数 live。未在脚本内夹带 GUI 探测。

实际顺序：先暂存/static/默认 plan；父会话授权后执行 `apply_product_live.py --apply`；第一 build 发现依赖头顺序问题，仅把三个头移到现 harness 块的 Window import 前并同步最终 inl；随后完整同配置重建通过。若以后在同冻结基线重放，应先审核 patch 和源码锚点，再用最终 apply 脚本；不要覆盖别人新的捕获修改。项目无新构建系统。

## 实际离线验证

完整构建复用 ignored `Build/ScrollFollow/build-motion-tracker.ps1`：vswhere 定位原生 ARM64 MSBuild，在同一 PowerShell 规范单一 Path、禁 node reuse，现有 `InkeysRepo.sln Debug|ARM64 /m:1 /nr:false /p:BuildInParallel=false /p:PreferredToolArchitecture=arm64`，normal host、10 分钟预算，无并发 MSBuild。

| 命令/范围 | 实际结果 |
| --- | --- |
| `build-motion-tracker.ps1 -Round product-live-first` | exit1 / 17,968ms。第一个有意义错误为新 inl 在 Window import 后包含 Host 头而重复定义 `FailedCleanupSignal`；不是 SDK/依赖环境问题 |
| `build-motion-tracker.ps1 -Round product-live-include-order` | **exit0 / 24,609ms**，仅既有 hashlib++ 三条 C4267；新 inl 无 warning，无 ILK/环境绕过 |
| `run_product_live_offline.ps1` 的 8 个 suffix/misplaced/trailing/duplicate/mixed 参数 | **全部 exit2 / stdout0 / stderr0**，103–684ms |
| `Inkeys.exe --scroll-follow-protocol-test` | **exit0 / 277ms**，包含既有实际 Business Tick/Frame/Tracker 控制矩阵，外部边界仍是它原本的纯测试接缝 |
| `Inkeys.exe --state-mode-production-test` | **exit0 / 110ms** |
| scoped `git diff --check --ignore-submodules=all` 与 final byte/hash 检查 | **exit0**；三个既有源保持 BOM/CRLF，Main 保持无 EOF 换行；新 inl UTF-8 无 BOM/CRLF |

构建原始日志 `Build/ScrollFollow/motion-tracker-build-product-live-first*`、`motion-tracker-build-product-live-include-order*`。离线原始结果 `Build/ScrollFollow/product-live-stage/offline-212057968/result.json` 及各 stdout/stderr，所有退出码均从真实 Process.Refresh/ExitCode 读取。没有重复旧全量 `--no-window`、保存长矩阵、x64 或 Release；本轮改动仅新 harness/分派，Headless 最终 hash 与此前冻结值一致。harness-off 仅静态核对返回2，未另建产物。

最终 SHA256：

| 文件 | SHA256 |
| --- | --- |
| `Inkeys/IdtMagnification.cpp` | `c8b1651708f2ef9ece15b577912195bf4c39ed2cdc6e768f1e1490e774c2f16a` |
| `Inkeys/IdtMagnification.h` | `662efc46ca1dffb090ba0f275ffc1c44a9f7ec107f375411a47198a35091d8ec` |
| `Inkeys/IdtMain.cpp` | `0bf398a0c3a31a7f0de1c0c8d1340c3169942eca81694115872dd74dab8c639a` |
| `Draw3.ProductLiveTest.inl` | `41192f8cf95bbd58eabc2050a6b9cf7fa8e092c1a1e460b1a8a60fbd61b72b36` |
| `Build/ARM64/Debug/Inkeys.exe` | `f84bf2037b033876ab3ef8bac2dc7a67d4f24ceb4319a416a6b1dba80e1ba8c6` |
| `Build/ARM64/Debug/InkeysHeadlessTests.exe` | `ed614767d28d07a27d60b0197f964097645b321a5229bd6502ba7e7e6727456a` |

## 下一放行边界

先独立离线 check 本夹具隔离/最早参数/真实 owners/ACK/清理，保留源码冻结。**人工明确回复解锁且父会话先完成正式 Capture 20 循环/Freeze 门槛之前，不运行下面的精确命令，连安全门试探也不运行。** 放行后父会话串行持有桌面，从仓库 cwd 只启动一个专用进程：

```text
Build/ARM64/Debug/Inkeys.exe --scroll-follow-live-test
```

父会话后续明确指定可见专用测试使用 `Normal`，与成功 Capture 扩展一致，避免 StartupInfo Hidden 干扰首个 Show。可直接复用现有 launcher：

```powershell
& Build/ScrollFollow/run-live-capture.ps1 -Round product-first-<fresh> -Flag '--scroll-follow-live-test' -WindowStyle Normal
```

`<fresh>` 必须换成新的唯一轮次名；命令仅作交接，尚未运行。launcher 先拒绝已有 Inkeys，只等待自己创建的 process handle 100s，重定向 stdout/stderr；夹具内90s截止只结束自己进程、exit3，不是正常清理通过。专用 source 必须正常取得自己的 foreground/实际 cursor hit/GT，否则安全失败，不通过修改外部窗口或安全层补救。不启动普通 Main、不运行其它应用。读取 root/result.log、两个经过专用 GT 的 source ROI、Business 原 verbose 和 cleanup 行，核对完整 exit。安全层重现就停止，等用户手动解锁。

## 交接后已知检查项（尚未由本实现者修改）

父会话已收到 Capture 全生命周期真实 PASS：41.388s，PID26916，20cycles/Freeze/readStop，全部专用窗关闭。这是另一份运行报告的证据，本报告不重述为产品实屏已测。当前产品源码/build/GUI继续冻结给独立 checker。

checker 在产品首次 GUI 前发现并接管两项必须修复的 harness 安全问题：`Wait` 的取消/失败可能被同次 true predicate 绕过，且输入前须统一复核取消/失败与实际 cursor 命中；安全异常须先 `service.BeginShutdown` 再 joins，防已进入 StateMonitoring 的提交继续 Show，正常 Off 仍保留真实 ACK。上述 hash 对应这些修复之前的本轮构建，不能当作修复后可运行版本。本实现者只记录交接、不并行修改；下一次 GUI 必须使用 checker 修复、完整 build 后的新 hash，并等父会话明确再次交接。

## 首次真实产品运行：前台门安全失败

父会话完成 Capture 20 循环/Freeze 前置后，明确交接本实现者独占 GUI；用户几分钟前再次手动解锁。checker 安全修复版完整 ARM64 build exit0/23.627s，交接 EXE SHA256 `77E0F580B5AC155E930335A212A48088EFAEFA4D448FBCA00DE9F5E08C1D96C1`；本实现者先只读 hash 校验相同，再正常宿主执行：

```powershell
& Build/ScrollFollow/run-live-capture.ps1 -Round product-first-unlocked -Flag '--scroll-follow-live-test' -WindowStyle Normal
```

实际结果 **exit1 / 2,748ms / PID4244**。原始日志 `Build/ScrollFollow/live-product-first-unlocked-test.log`、`-stderr.log`、`.json`；私有目录 `Build/ScrollFollow/product-live-4244-212622359/result.log`。

- source 实际 paint sequence1、offset `(256,512)`、texture hash `04f97d148251701f`；原 Mag pair 创建在 owner TID28720，source 不在5项角色过滤列表，辅助窗 transparent1，正常 DComp 能力探测为1。
- **失败在 `stage=window-service / reason=own-source-foreground / security=0`**：对自身 source 的 SetForegroundWindow 未使它成为真实 foreground，因此严格前置门立即拒绝继续。`security=0` 仅是此次查询结果，不推出系统安全状态的普适结论。
- ScreenSourceGT、PNG、StartProduct、StateMonitoring、MagnifierThread、Business RequestToggle、真实 SendInput 都未执行。原 Mag HWND 创建成功不等于正式采样；这一失败也不是 Capture/Tracker/ACK 失败的证据。
- 自动清理后 `product_running=0/service_running=0/mag_ready=0/capture_occupied=0/log_failed=0`，WindowService 原 owner 的窗销毁诊断完整返回。state/mag thread 未启动，故其 returned=0 是本次未到达，不是假造 join 成功。
- 已立即向父会话报告并停止，没有重试、改 source/build、操作外部窗口或试图绕过 foreground/安全门。后续诊断/夹具调整须按父会话新交接串行进行；本次不放行 Tracking/wheel/Off 或墨迹验收。

## 正常系统单击、早期 UI 就绪与真实最小链 PASS

父会话批准先专用 GT、实际 cursor 命中后正常 OS 单击激活自身 source；不发 ALT、不用 AttachThreadInput 或其它前台限制绕过。Down单独成功才记录 ownLeftHeld，Up每次最多3次；异常清理可再归还自己 held，失败保持 unproven，不能通过。所有新 Down 前检查已有 physical Left，避免归还用户原本的按下。最终 Off 增加 x≈baseX/y≈baseY+16、无 clamp 和再次专用 SourceGT。

单击版 `product-live-own-click-lifetime` 完整 ARM64 build exit0/22.959s，8坏 argv + protocol/state 均通过。随后 checker 放行的实际 `product-click-unlocked` 运行，EXE `0F9BA526B490BF649B795559FAE668408E7D3835ABB90D9275A6F939C157EA18`，PID72368：单击激活和 GT 成功，但 Initializing 8s 未进 Capture，异常退出 `0xC0000005` /12.678s，无 cleanup 完成行。正确私有根为 `product-live-72368-213134453`；早期通讯里曾把 tick 写错，已更正，不用该错误路径作证据。

只读取证找到本次 WER 与 Application Error 1000：18:34:31.555，PID `0x11AB0=72368`，模块 ntdll.dll 10.0.26100.9711，偏移0x2CB00，异常c0000005；Report.wer SHA256 `9F1CDE439187918F77A5DDC258BD2FFCCF255CC5F86D61348F4C8FBAC9AEE782`，字段保留 `Build/ScrollFollow/product-live-stage/crash-evidence-72368.json`。归档/本次root/LocalAppData均无dmp。未找到cdb/WinDbg安装，有VS原生ARM64 llvm-symbolizer/PDB但没有线程上下文，不能还原这次AV栈或把wordMap访问认定为唯一崩溃指令；没有改全局WER/注册表/调试设置。

独立研究确认两项实际缺口，父会话授权最小修复并由checker复核：Business RefreshUi原裸 UpdateRendering(true) 在Host早于Bar时仍访问未初始化粗细文字，替换为已有 `Inkeys::UI::Bar::RefreshToolState()`，沿 contentStateUpdatesReady 保护但仍 Notify/Request，UI就绪时保持完整刷新；fixture COM从STA改为普通Main和RealtimeStylus实际使用的MTA。旧运行RTS `0x80010106` 因模式冲突，不能称硬件不可用。

`product-live-ui-ready-mta` 完整ARM64 build exit0/25.297s；8坏argv、protocol/state通过。EXE `84041939F4BEC23BBCCB4840D312354C6EFB0A81DF7905C2CEE84D34633AE9B2`；Business `FFE8124961A426D5D505D8C09219A7A2E61DBB5FCD8EA3E14E964299D87A77DC`；inl `25725365C4DA9E325CA11B906458D31DBB884B9D9B7F0E100F4D191FFA8BCE93`。

父会话再次明确交接后执行 `run-live-capture.ps1 -Round product-ui-ready-mta-unlocked -Flag '--scroll-follow-live-test' -WindowStyle Normal`：**exit0/14.081s/PID53500**，原日志 `Build/ScrollFollow/live-product-ui-ready-mta-unlocked-test*`。本轮源激活真实Up accepted1；RTS正常 `input_mode=1 (RtsExtended)`，无changed-mode错误。

- Business/Capture都是session1，Tracking时真实output1/revision3、ACK1、空页pair=(0,0)，正式accepted30/rejected0/API0。
- 真正 `WM_MOUSEWHEEL=522`，tagged1、delta=-120、坐标(1440,960)，source从(256,512)到(256,528)。Tracker total=(0,-16.0000)，owner latest/applied30、viewport=(0,16.0000)、clamp0。
- Off真实逆ACK1、pair=(1,0)、output0/revision4，phaseComplete、final viewport仍(0,16.0000)。End27 owner帧、recovery0是本次调度结果，不解释为FPS。
- before/after/Off屏幕GT全部99/99，精确ROI均通过；图片只限已确认的源内容。诊断有2条contention丢弃记录，原事实日志完整；不声称逐条verbose无损。
- 正常cleanup：StateMonitoring与原Mag均真实returned1/failed0，Product/Service/MagReady/CaptureOccupied/ownLeftHeld全部0，result0。没有再现AV。

这放行有限空页真实Capture→Tracker→Business→实际ACK→OS wheel→Off链。激活点击不是画笔证据；Presentation墨迹可见、A/B或文件本轮尚未覆盖。

## 有墨迹门槛：已构建、待审后GUI

父会话批准在上述PASS基础上实施独立 `--scroll-follow-live-ink-test`，旧empty CLI保留。当前仅harness新增真实OS A/B helper、两步wheel(+32再-16)、屏幕core对比和原Exit文件只读观察；没有启用/发送HiddenTestContact，真实OS不被RTS消费时直接记录失败，不自动切后端或混入seam。只开原有runtimeMetrics128/RTS trace观察Down/Terminal/Recycled/contactSeen/完成Drawing/canUndo/真实Present。

A/B ROI明确处于两组99点采样行之间，与左上精确ROI不相交；运行时逐点验证这一布局，不跳过任何既有GT点。每笔前再核鲜活SourceGT、每次OS Move/Down/Up核实际自身Drawpad命中、原guard/cancel/physical-held/ownUp归还保持。已知灰底和红/蓝笔走廊逐像素验证InkROI；未知像素或core不足拒绝保存。实际画面core按真实GT位移做双向最近距离，max≤2px/中位数留日志、像素数量约束保留，既要非空Tracking真实pair=(0,1)，也要Off实际pair=(1,0)和最终非零viewport16。

正常Off之后真实OS画B；不新增保存动作，沿本就启用的StopProduct→Exit/drain，窄读唯一实际index entry/真实UInk，核Exit、同workspace/page GUID、保存viewport、真实A/B两条Pen多点以及B首点=实际clientDown+保存viewport，再v10 Read Complete/Import Success。不开放任意Canvas读取或冷载API，不扩完整历史/长保存矩阵。已有只读研究详 `live-ink-input-observation.md`。

`product-live-ink-first` 完整ARM64 exit0/23.831s；随后补鲜活每笔GT和Down紧前physical-held复核，最终 `product-live-ink-preinput` 完整ARM64 **exit0/14.336s**。新inl无warning，原hashlib++/Setting/Main转换warning保留。16坏argv全部exit2/stdout0/stderr0，protocol exit0/267ms、state exit0/118ms；diffcheck通过。最终冻结 EXE `B6EBE54EDFC32465539AB8AEA4DAA1752475B0DD548E5BDC2BDFB9FED05A0154`，inl `C0CBB4EBE3A751091C9441AFA6F8F9231175D4FE290CB41C4DD19C1DC80A28B4`，Main `461015F3B557FFFA25FF40F135CD4FAA6C31347D8116EA23D14A651C000C8486`，header `E770139B74FC7215F58DAAE2C80D12601775544EF0BBFFDB2EBD3C9DDD908DB6`。当前尚未执行这个新GUI入口，等待checker快速只读审查及父会话最终GUI交接。

本次仍未运行：实际 Tracking/F0/ACK、系统 wheel、实屏 viewport误差、真实 Off、线程 cleanup、墨迹A/B、可见 Presentation、最后目标文件读回/冷载、长滚动/反向/水平/relay、UI3/Bar按钮与快捷键、Edge/Word/PDF、真实笔/触控、旧 Windows/WOW64、显示/DPI/设备退役。首版成功也只放行其空页最小链；应再分步加入有墨迹的真实输入/明确 seam、像素及原保存证明，不能一次宣称全功能通过。

## 新 ink CLI 首试：工具发布后的 ACK 等待失败

独立checker放行C0CBB4版后，父会话明确交接执行 `run-live-capture.ps1 -Round product-ink-first-unlocked -Flag '--scroll-follow-live-ink-test' -WindowStyle Normal`。实际 **exit1 / 6,521ms / PID29296**，原始日志 `Build/ScrollFollow/live-product-ink-first-unlocked-test.log`、`-test-stderr.log`、`-test.json`，实际新建私有root为 `Build/ScrollFollow/product-live-29296-214909218`。launcher在StartProcess前记录同一EXE的SHA256 `B6EBE54EDFC32465539AB8AEA4DAA1752475B0DD548E5BDC2BDFB9FED05A0154`，LastWriteUtc为2026-10-22T11:00:18.8422045Z；未重编或混入其它binary。

before-product源GT99/99和精确ROI通过，自身激活单击Up成功；真实Host/RTS/MTA启动，input_mode=1。pen-ready时Business Off、primary requested/ready均0/2、contentRevision=presented=0、projection0、actual_ack=1、pair=(1,0)。随后 `stage=real-os-ink-a / reason=ink-a-real-tool-ready / security=0`，同一输出/内容保持就绪，但actual_ack=0。RTS日志contacts=0/0；尚未执行DrawOsInk的A Down。因此不能把本次称为RTS不消费、墨迹/捕获/Tracker失败，不能切合成输入求过；Tracking、wheel、InkROI PNG和真实Exit文件验收均未到达。

正常cleanup为StateMonitoring returned1/failed0、原Mag returned1/failed0，product_running/service_running/mag_ready/capture_occupied/own_left_held/log_failed均0；没有AV，没有夹具遗留的held或窗口。源/build/GUI在该次失败后冻结，未重试。

只读定位：IdtState的SetPenWidthSelectionImpl在合法Pen写宽度、增真正modeRevision并PublishDraw3State，SetPenColor也写真实slot并Publish；两者成功returntrue，不执行Reconcile。Host ApplyState只在selectionChanged时发布runtimeRevision；纯宽度/颜色没有新的内容或输出代，StateMonitoring只在runtimeChanged或presentationRetryPending时Reconcile。ScrollFollowPresentationAcknowledged同时要求当前mode/bridge与已完成WindowService事务记录完全相符，因此原工具样式变更后，fixture直接等旧ACK自行更新没有真实驱动。此为夹具驱动缺口证据，不删除ACK条件，不手写ACK，也不手动Tick。

已向父会话提交的最小候选是：A/B真实setter成功之后请求现成RequestScrollFollowPresentationRefresh，使真正StateMonitoring重新发布并沿原Reconcile/WindowService事务取得新ACK；增加有界setter/等待分开日志及真实mode/bridge/width/color/PrimaryReady事实。尚待父会话交接后应用和独立check；不引入生产新API。

## 样式 ACK 归属确认与最小生产修复（待新GUI）

后续只读研究 `live-style-ack-investigation.md` 确认普通主栏的若干颜色/粗细路径只setter+记忆保存+Bar渲染，不统一补Window ACK；故上节“fixture驱动缺口”只是初步候选，最终属于新增精确ACK合同在稳定样式后的收敛缺口。Available本身不看ACK，下一Toggle又会重新投影，不能扩称按钮必然禁用。未采用fixture强制Refresh或删ACK条件。

父会话批准仅在IdtState的StateMonitoring原 `runtimeChanged || retryPending` 判断追加当前ProductRunning且精确ACK过期的事实谓词。它沿原250ms循环进入已有Reconcile/WindowService，既不Publish额外state、不置dirty，也不改retry清除协议。旧成功回执若覆盖了并发新发布的bool请求，当前mode/bridge/projection与旧ACK仍不符，下一轮重新核对；稳定成功后没有额外重核。ProductOff仍沿原早退和Acknowledged返回true，不持续拉窗口。inl只增加setter返回值/等待结果与实际mode/bridge样式的有界诊断，原setter顺序、短路及PrimaryReady+实际ACK成功要求保持。

`product-live-style-ack` 完整InkeysRepo.sln Debug ARM64原生MSBuild **exit0/15.559s**，日志 `Build/ScrollFollow/motion-tracker-build-product-live-style-ack*`，仅原hashlib++ C4267，未改依赖/工具链。16坏argv均exit2/双输出0B，protocol exit0/264ms、state exit0/131ms；原始目录 `Build/ScrollFollow/product-live-stage/offline-215493359`。无exact GUI旗标运行。

冻结EXE SHA256 `D6A348E46F78E0FC53F5C39B921EFAB11A7BB4B498DE4CACC03940964FA456ED`；IdtState `3E7814E507FBC2B1C909ACB3B95A1019A5E1B56B63FBF796D2D98EA9B3443762`；inl `6C655BF5449AEF8A401DDC0B5A35854069574BD6C99D0A7C280F847B62956D7A`。IdtState BOM/CRLF、inl无BOM/CRLF、两者EOF保持，diffcheck通过。新版本须独立checker放行和父会话重新交接后才做受控GUI；尚无样式后ACK恢复或实际A/InkROI/文件的新runtime证据。

## 样式 ACK 修复版首个重试：初始化前安全阻断

checker放行IdtState3E7814E/inl6C655BF后，父会话明确交接同一D6A348E binary，正常宿主运行 `run-live-capture.ps1 -Round product-ink-style-ack-unlocked -Flag '--scroll-follow-live-ink-test' -WindowStyle Normal`。launcher的file-before-StartProcess SHA256仍D6A348E，LastWriteUtc=2026-10-22T11:13:15.8833640Z。

实际 **child exit4 / 159ms / PID79380**，stdout0B、stderr54B，唯一诊断为 `[ProductLive] blocked=security-before-initialization`。原日志 `Build/ScrollFollow/live-product-ink-style-ack-unlocked-test.json`、`-test.log`、`-test-stderr.log`。tool外层返回1与child JSON exit4分别保留，不推断该wrapper状态的额外原因。

最前安全检查即拒绝继续，未新建私有root/测试窗，ToolFacts、A Down、ACK恢复、Capture/Tracker/轮动、InkROI和Exit文件均未到达。不能把它计为修复失败或通过。没有Computer Use、重试、桌面/外部窗口操作或防锁措施；源码/build继续冻结，等待人工解锁后的新明确交接。

## 用户再次回复解锁后的短 ink 重试与一次只读归因

父会话收到新的明确解锁回复后，再次只交接现有D6A348E ARM64短ink入口，没有apply长case或重建。执行轮次 `product-ink-style-ack-second-unlocked`，启动前哈希一致；实际 **child exit4 / 174ms / PID59040**，stdout0B/stderr54B，唯一诊断仍为 `[ProductLive] blocked=security-before-initialization`。日志 `Build/ScrollFollow/live-product-ink-style-ack-second-unlocked-test*`。tool外层返回1与JSON中的child4分别保留，未归因wrapper。

未创建私有root/测试窗，未发送A/B Down，ToolFacts、样式后ACK恢复、真实墨迹像素/滚轮/文件均未到达。已立即停止，没有第二次试跑、截图、防锁或外部窗口操作。

父会话随后明确只授权一次正常宿主原生只读元数据查询，不新开GUI、不枚举、不截图/输入。该查询exit0/0.535s，UTC2026-10-22T11:31:46.461462Z，输出在 `Build/ScrollFollow/product-live-stage/long-stage/security-metadata-216574656000000.json`：

- WindowFromPoint命中HWND0xb800fe，TID88008/PID13716，class=`LockScreenBackstopFrame`，exe=`C:\Windows\explorer.exe`，Query成功。
- GetSystemMetrics返回primary1440×960、查询point(720,480)、rect(0,0,1440,960)，visible=true、iconic=false、DWM cloaked=0且HRESULT0。
- query当前thread、hit thread和input desktop名称均Default，读取均成功。class匹配为true，security exe列表匹配为false，原predicate在该次查询时会因class直接阻断。

坐标证据边界：Python查询进程没有调用Main的EnsureProcessDpiAwareness，以上是该进程API返回的逻辑尺寸/坐标；不能把其中心比例认定为已证明与DPI-aware harness完全相同的物理中心命中。完整屏幕backstop命中事实保留，不以Default桌面名/未cloak宣称已经解锁，也不据此削弱guard。源码只读定位还确认：LiveSecurityInterfacePresent只核中心hit class或该hit的security进程名，不核visible/iconic/bounds/cloaked/desktop；Product最前阻断日志没有具体命中，故原PID59040日志不能反推出同一HWND。没有另发查询或重试。

长滚动准备目前仅在ignored `Build/ScrollFollow/product-live-stage/long-stage/`：基于6C655BF保留短case生成五个proposed文件和patch/manifest，包括独立strict long CLI、二维wheel/mask、十个primaryHeight往返和有界240s自截止的草案。它们**未apply、未build、未run、未验收**，后续apply脚本/纯规划验证与review尚待完成；当前短ink门槛也未通过，不能将草案算成长程成功。所有真实source/build/GUI保持冻结，等待父会话收到用户桌面状态确认后的新交接。


## 正常桌面确认后失败分型诊断（2026-10-22，未重试 GUI）

用户明确回复“现在已显示正常桌面，继续测试”后，父会话使用原 D6A348E binary 运行 `product-ink-desktop-confirmed-220145046`。PID56220，实际root `Build/ScrollFollow/product-live-56220-220145468`；source paint sequence1成功，原Mag pair创建，随后在window-service阶段以 `fixture-owner-failure-or-cancel / security=0` 停止。没有到StartProduct、GT、A Down或Capture/Tracking。旧失败字符串合并 sourceCancelled/sourceFailed/stateFailed/magFailed/offSignal 五类，不能凭现有日志归因为PrintWindow、Mag、offSignal或用户操作。

本次只改harness `Draw3.ProductLiveTest.inl`，未改生产源码/flags/窗口顺序/guard/输入/cleanup。新增CheckFixture五类失败快照；Close/Esc、外来wheel、线程WM_QUIT的来源、message/tag；原source纹理resize及BitBlt/GdiFlush失败的分开结果和即时错误值。Close/Esc和外来wheel仅首个取消记来源、resize仅首次失败记日志；paint失败后原sourceFailed闸门继续阻止后续复制，CheckFixture仍立即抛原异常。GDI错误码仅诊断，不单独断定原因。没有清offSignal、吞WM_QUIT/外来wheel、放宽GT/安全或追加输入。

精确旧基线由逆六处诊断增量生成，并校验完整bytes SHA256等于已冻结6C655BF5449AEF8A401DDC0B5A35854069574BD6C99D0A7C280F847B62956D7A；仅用于审查，不写回源。快照/六处diff/manifest在 `Build/ScrollFollow/product-live-stage/fixture-diagnostic/`。最终inl SHA256 `FC28A0BE3C5D2F3625BC74890FF81B244A9599B57D10193E13838A45BFF38BDE`，UTF8无BOM、仅CRLF、EOF保持；scoped diffcheck通过。

首次未限首事件的诊断版完整ARM64 build exit0/26.331s；最终有界版 `product-live-fixture-flags-bounded` 再完整 `InkeysRepo.sln Debug|ARM64`，ARM64原生MSBuild、同PS Path规范/无node reuse，**exit0/20.509s**，只有原3条hashlib++ C4267。最终EXE SHA256 `230902583FBACC179F1F36CBB8323EAB627463C26DB513448392B8F4D1080791`。

现有offline runner结果 `Build/ScrollFollow/product-live-stage/offline-229803921/result.json`：16坏argv全部exit2/双输出0B，protocol exit0/255ms、state exit0/136ms。没有运行精确live单参数，没有重跑157s无窗口全量（本次仅诊断），没有GUI/截图/输入。此版本交独立checker，只有父会话后续明确交接才可再运行短ink；仍未证明样式ACK收敛、真实墨迹A/B或Exit文件屏幕链。


## 诊断版一次实屏：真实A、实际辅助交接、向前32像素通过；反向前GT失败停止

独立checker放行FC28A0BE/EXE23090258后，父会话明确交接一次原strict `--scroll-follow-live-ink-test`，没有重建或apply long草案。正常宿主执行轮次 `product-ink-fixture-diagnostic-229909500`，**PID43872 / exit1 / 22.889s**，原始 `Build/ScrollFollow/live-product-ink-fixture-diagnostic-229909500-test{.log,-stderr.log,.json}`；私有root `Build/ScrollFollow/product-live-43872-229909937`。launcher启动前binary SHA256严格23090258，未混入旧binary。

实际已到达的新证据：

- A的真实SetPenWidth/SetPenColor均成功；A-after-setters实际ACK0，原StateMonitoring后A-after-wait在3000ms预算内ready1/实际ACK1，同modeRevision3/bridgeRevision5，无fixture手工Refresh/ACK。此为本次样式ACK收敛正证据，不是所有UI场景或长期稳定性覆盖。
- 真实OS SendInput A被RtsExtended input_mode1消费，客户端Down=(768,706)，实际down/terminal/recycled/contact各从0到1，完成Drawing kind1、canUndo1、ready1。RTS最终18callbacks/16packetSamples、publishOk18/failed0、slotDrops0；trace只保留8条/drop10是有界观察日志的保留上限，不作实际输入丢失10条解释。没有启用contact seam或合成contact，亦非硬件笔证明。
- A主窗ROI(720,541,1440,197)已知灰背景+本笔红色、unknown0/core18772；真实Tracking后辅助窗pair=(0,1)、ACK1、content1，A辅助屏幕ROI与主窗相同。真实交接双向37544core点中位/最大误差均0px。
- 一次真实tagged WM_MOUSEWHEEL delta=-240使源(256,512)->(256,544)，实际paint sequence2。Tracker/owner稳定后A向上32px，仍core18772/unknown0，双向37544点误差0px。独立离线读取三张实际PNG：主窗/辅助PNG bytes完全相同，向前图core集合严格等于原core平移(0,-32)，三图alpha全部255；结果在root/ink-image-offline-check.json。只对这组专用图案/笔划和32px成立。

**停止点**：开始reverse16时的原鲜活SourceGT变为78/99且左上精确ROI失败，立即 `reason=ink-wheel-source-gt / security=0`。原门禁在发送反向滚轮之前，因此没有reverse输入、B、正常Off成功断言或ReadInkExit完整验收。没有保存未知失败屏幕、重试、移除GT或防锁操作。失败不是先前合并fixture flags，新增fixture_failure/cancel/source_failure日志均未触发；现有证据无法判断GT变化来自何种窗口/内容，不归因为锁屏/用户操作/渲染/Mag。

停止事实：Business仍Tracking，Capture accepted62/rejected0/API0，最新/applied62，total=(-0.0025,-31.9993)、viewport=(0.0025,31.9993)、clamp0、实际ACK1、pair=(0,1)，last完整Read耗时68996us（单次，不能当FPS）。失败清理沿原Exit写了单笔文件（日志commit成功），但没有调用本case的两笔UInk验收，不能称A/B文件通过。StateMonitoring与原Mag returned1/failed0，Product/Service/MagReady/CaptureOccupied/ownLeftHeld/logFailed全部0，进程已退出。源/build冻结，未自行重试。


## GT停止点只读取证与后续有界诊断版（尚未实跑）

PID43872现有日志仅两次source paint和一次tagged wheel，forward PNG到reverse GT之间没有新paint/wheel或fixture/cancel/GDI失败日志。CompareInk在main线程同步双向扫描/5×5搜索/sort，未泵消息；旧日志没有其开始/结束QPC或耗时，不能从accepted数量认定它阻塞多久。失败Facts证明当时主窗隐藏/辅助显示/ACK1，但source矩形、GT失配RGB和各点命中窗口未记录；稍后cleanup中的Draw3窗口矩形不能补证先前源内容或命中。未归因外部窗口/用户/锁屏/Mag。

父会话授权最小harness诊断增量：GT原99个样本中最多保留前三个失配坐标与actual/expected RGB；精确crop只复用既有缓冲的首个坏pixel，不另取屏幕像素/未知图；失败后只读WindowFromPoint命中HWND/class/PID、是否已知source/Drawpad/Presentation/Mag、rect，并记录source paint seq/offset/rect。命中字段称hit_now，是读取后的查询瞬时元数据，不声称与之前GetPixel原子一致；不记录窗口标题。原99/99、精确ROI、失败即停和正常流程不变。CompareInk仅补QPC elapsed_us，不改搜索、阈值或消息调度。

FC28完整before、精确diff/manifest在 `Build/ScrollFollow/product-live-stage/gt-diagnostic/`；最终inl SHA256 `E969322714EA844B92A01CE14CFA47297CA49584FE19CEFBC1E87807E119597A`，UTF8无BOM/纯CRLF/EOF保持，diffcheck通过。完整ARM64 `product-live-gt-mismatch` build **exit0/36.269s**，只有原三条hashlib++警告；EXE `EF5A2E598F37C4FB255FD4FDFDB00DA24C25758D41BD9E2FE1706B33B6AA849C`。最小离线 `offline-230171343/result.json`：16坏argv全部exit2/双输出0B，protocol0/450ms，state0/115ms。没有GUI运行，没有apply long，源码与binary冻结交checker后待父会话交接。


## GT诊断版一次实屏：A反向和正常Off通过，B原像素分类unknown12停止

checker放行E9693227/EXE EF5A2E59后，父会话明确交接一次原strict短ink，轮次 `product-ink-gt-diagnostic-230285000`，**PID62840 / exit1 / 32.681s**，root `Build/ScrollFollow/product-live-62840-230285718`。启动前EXE SHA256严格EF5A2E59，未重建/改source。原日志 `Build/ScrollFollow/live-product-ink-gt-diagnostic-230285000-test*`。

本次没有GT失配，各实际GT99/99+精确ROI1；只称之前GT问题未复现，未宣称修好。A和B样式setter均真实成功，原StateMonitoring均让精确ACK从0收敛1。A真实OS/RTS输入完成，primary和实际Tracking辅助交接core18612/unknown0，handoff双向37224点max0。源一正一反两次真实wheel/paint：原y512->544->528；forward32图core19110/unknown0，双向37722点中位0、最大1px（此前简报写0已立即更正）；reverse16 core18612/unknown0，双向37224点max0。

正常Business Off与原窗口逆交接完成，actualACK1、pair(1,0)、最新/applied75、viewport(0,16)、clamp0、工具/页contentRevision等最终不变量通过；Off主窗A core18612/unknown0、max0。Capture accepted75/rejected0/API0。CompareInk本次handoff/forward/reverse/Off分别11.692/11.228/11.044/14.913ms，不从这次推断旧失败运行耗时。离线PNG独立重读全部A图core/最近距离与日志一致，alpha全255，详root/ink-image-offline-check.json。

B在正常Off后真实OS/RTS完成，down/terminal/recycled/contact各由1到2，Drawing完成/undo/ready均1，clientDown=(768,1346)。B已读ROI(720,1181,1440,197)、lineY1346、core19105，但原像素分类得到**unknown12**，因此立即按原规则 `ink-pixels-or-dedicated-roi-unproven / security0` 失败；B图未保存，ReadInkExit未执行。cleanup沿真实Exit捕获2strokes/5903bytes并commit成功，只是日志事实，不是完整两笔文件/首点验证通过。两线程returned1/failed0，Product/Service/MagReady/CaptureOccupied/ownLeftHeld/logFailed全部0。未重试或放宽门禁。

只读B分类：蓝墨迹资格为b>r+4且b>g+4，走廊absY<=24、局部x>=16且x<w-16；若非(墨迹且走廊)，任意通道与已知世界TexturePixel灰度差>3即unknown。core另取强蓝阈值并计数。现有日志没有12像素的actualRGB/坐标/走廊，不能判断真实污染或低覆盖边缘；严格色差>4和背景容差3之间存在理论分类边界，但不能把它认定为本例原因。后续已获父会话授权只补既有ROI缓冲最多12坏pixel的有限诊断，不另读屏/保存图/改分类。未apply long。


## B未知像素有界日志版（待checker/新交接）

父会话授权后，仅在ReadInkScreen原循环记录最多12个原unknown pixel的local/screen坐标、actualRGB、expectedGray、原ink/core/corridor判定、b-r/b-g和absLineY；日志只在原unknown或core不足失败分支输出。原灰度容差3、色差4、强core阈值、GT、ROI和失败抛错完全保持，未保存失败B图、未加截图/输入。E969完整before及精确patch/manifest在 `Build/ScrollFollow/product-live-stage/ink-unknown-diagnostic/`。

最终inl SHA256 `E7C25A4E83D85D7ABA68FE619DF4C1E4CF1CFBE8ACAF1934FFB2D2E340CD3A43`，EXE `F568FE30399BDAFBC04228F4FDBF588733465E549873585279FF7D072D61F543`；UTF8无BOM/纯CRLF/EOF保持，diffcheck通过。完整ARM64 `product-live-ink-unknown` build exit0/23.228s，仅原三条hashlib++ warning；最小offline-230520984/result.json，16坏argv全exit2/双输出0B，protocol0/301ms、state0/121ms。一次sandbox runner在执行普通读/报告前CreateProcess错误2/3，未执行该次命令；明确系统PowerShell正常host完成后续，未改项目环境配置绕过。源/build冻结交独立checker，尚无新GUI，没有apply long。


## 坏像素诊断实跑：B四像素全部命中严格分类边界，未改阈值

checker放行E7C25A4E/F568FE30后，父会话交接一次原strict入口，轮次 `product-ink-unknown-diagnostic-230606140`，**PID10832 / exit1 / 34.528s**，root `Build/ScrollFollow/product-live-10832-230606640`。原日志 `Build/ScrollFollow/live-product-ink-unknown-diagnostic-230606140-test*`，启动前EXE严格F568FE30，无重建/改source。没有GT mismatch，A真实输入/实际辅助交接/forward32/reverse16/Off都再次到达；各core18939/双向37878点max0，比较耗时15.419/11.336/11.307/11.320ms。Off actualACK1、pair(1,0)、final viewport(0,16)、accepted75/rejected0/API0。

B真实RTS完成后，原屏幕ROI检测core18972、unknown4，仍按原规则失败，不保存B未知图、不ReadInkExit。四条记录来自既有buffer：

| screen | RGB | expectedGray | b-r / b-g | corridor / ink / core | absLineY |
|---|---|---|---|---|---|
| (771,1337) | (31,32,36) | 32 | 5 / 4 | 1 / 0 / 0 | 9 |
| (2113,1348) | (56,57,61) | 57 | 5 / 4 | 1 / 0 / 0 | 2 |
| (1944,1353) | (198,199,203) | 202 | 5 / 4 | 1 / 0 / 0 | 7 |
| (770,1354) | (193,194,198) | 197 | 5 / 4 | 1 / 0 / 0 | 8 |

四个点都在走廊，b-g恰好等于4，被严格b>g+4拒绝，同时至少一通道与灰背景差4，又超过背景容差3。这直接证明本次分类边界被触发；蓝色方向和低覆盖混色相符属于有证据推断，不能泛称未知像素都是真实AA或据此放宽任意污染。本次没有阈值修改、自动重试、未知截图或真实文件研究重复；父会话另有research独立读PID62840两笔文件/RTS量化。

cleanup真实Exit捕获2strokes/5879bytes并commit成功，但无本case完整ReadInkExit验收；两线程returned1/failed0，Product/Service/MagReady/CaptureOccupied/ownLeftHeld/logFailed全部0，进程已退出。源码与binary继续冻结，未apply long，等待父会话决定最小夹具分类修正及独立check。


## 受限墨迹边缘与 native mouse Down oracle 修正（2026-10-23，待独立 check/GUI）

只改内部专用 `Draw3.ProductLiveTest.inl`，没有修改产品 Mag、输入、保存或 GPU。上次四条 B 坏像素确实落在 `b-g==4` 且灰底差4的分类边界；新增 KnownInkEdge 用当前笔固定RGB和逐点已知灰底拟合**同一覆盖率**，仅在原 `corridor && !ink && backgroundMismatch` 接纳 alpha∈[0,.05] 且每通道残差≤1的低覆盖边缘。不会跳过GT点，不影响强core/数量/双向≤2px跟随误差，走廊外/方向错/任意灰偏移仍拒绝；日志新增 known_edge 数量及最大残差。不能泛称所有未知像素均为AA，也没有更改系统截图。

实际文件研究已证明旧整数鼠标Down的.001px不是RTS输入坐标：A偏0.015747px、B偏-0.034668px在RTS解码时已经存在。新的受限OS鼠标夹具记录 Display 实际effectiveDpiX/Y，持续检查同primary monitor、origin0、物理尺寸/方向/DPI稳定，Down紧前确认真实Drawpad client origin0/尺寸1:1；不假定任意数位板比例等于Display DPI。每轴以研究 `live-native-down-oracle.md` 的一整HIMETRIC单位q=DPI/2540，加float比例表示、乘法、viewport相加舍入计算上界；finite输入、DPI正值及raw估计R<2^24是前置。实际A/B存储/独立整数expected/每轴bound在断言前全部日志。只替换A/B native cursor→RTS首点oracle；文件viewport仍.001，身份/两Pen/v10 Read Complete/Import Success与其余隐藏精确坐标测试不动。

忽略目录 `Build/ScrollFollow/product-live-stage/ink-oracle-fix/` 保留E7C25A4E原始before、`oracle-delta.patch`、`verify_oracles.py`及`offline-oracles.json`。540个不同DPI/正负坐标/viewport的floor/ceil/nearest前向整数HIMETRIC→float32测试均在推导界内（最大error/bound=.941333）；720个±1px/±16px坐标污染全部拒绝。四个实际未知RGB和其余方向/灰偏移/绿偏移/超过5%/错笔色等合计10例由独立逐通道alpha可行区间复核，均符合预期。192DPI样本A y上界.075761525px、B y上界.075918408px；这些是受限模型界，不是扩大Tracker或文件viewport误差。

完整原生ARM64 `InkeysRepo.sln Debug|ARM64` 使用原有PATH规范化/no-reuse脚本，轮次 `product-live-ink-oracles-1023`，**exit0 / 22,939ms**，原始 `Build/ScrollFollow/motion-tracker-build-product-live-ink-oracles-1023*`；仅原三条hashlib++ C4267。最小CLI原始 `product-live-stage/offline-5450515/result.json`：16坏argv各exit2且双输出0B，protocol exit0/232ms、state exit0/101ms。`git diff --check --ignore-submodules=all`通过；不带该选项的初次命令受HiMsg子模块读取权限影响，不属于源码/编译错误。inl UTF8无BOM/纯CRLF/原EOF保持。

冻结inl SHA256 `D83E9F6555F5624DD6A48201FEC78E97E13F540EF1AFFE707C7A7A11C536EBB3`；ARM64 EXE `03DEA84FEA607475304C5E58B45C7F1F97B9408F124E47723347ECE8E4197251`。未运行GUI、未apply旧long stage、未commit/push。等待独立check和父会话显式GUI交接；本节离线通过不等于B屏幕/实际Exit文件或完整short case已通过。


## 修正后首次完整 short ink 实跑 PASS（2026-10-23）

独立check正式放行D83E9F65/03DEA84F，父会话明确交接一次GUI。实际轮次 `product-ink-oracle-fixed-1023-5561046`，**PID22200 / exit0 / 32,320ms**；launcher启动前hash仍03DEA84F、LastWriteUtc=2026-10-23T00:42:28.6692318Z。唯一私有root `Build/ScrollFollow/product-live-22200-5561515`，原始日志 `Build/ScrollFollow/live-product-ink-oracle-fixed-1023-5561046-test*`。没有重建、重试或long stage应用；正常桌面安全门通过，全程只操作专用窗，未使用Computer Use或防锁。

实际Display DPI192/192，物理主屏2880×1920且整个输入/文件核对期间同geometry/DPI。A真实SendInput被原RTS消费并完成Drawing；actualACK1进入非空辅助窗口pair(0,1)。A primary/selection/forward32/reverse16/Off五图core全18648/unknown0/known_edge0，四次双向比较各37296点、中位/最大误差均0px，耗时14.854/14.551/14.737/14.846ms。真实源wheel y512→544→528，owner latest/applied75、viewport(0,16)、clamp0；Capture accepted75/rejected0/API0。每条屏幕GT均99/99及独立256×192精确ROI通过，不省略任何采样。Off真实逆ACK1、pair(1,0)，工具与content不变量通过。

B在Off之后真实SendInput/RTS完成（Down/Terminal/Recycled/contact全部从1到2），core18750、unknown0；仅13个点由新低覆盖固定笔色模型识别，最大通道残差.6982。现有8PNG（5A+1B+2源）只保存通过全部专用ROI断言的图。A/B actual_down_oracle：整数client(768,706)/(768,1346)，storedA(768,706.015747070)，storedB(768,1361.965332031)，expectedB(768,1362)，每轴bounds A(.075776536,.075761525)/B(.075776536,.075918408)均通过，未改保存viewport.001。

沿真实StopProduct→Exit捕获2笔并commit，严格唯一index序号/session daily各1，读取 `AutoSave/desktop/2026-10-23/084502678_25cee08f.uink`，两Pen227/225点、文件/workspace/page身份、viewport(0,16,1)、ReadComplete/ImportSuccess均通过，actual_exit和bounded_case真实PASS。没有手工目标/ACK或contact seam；这证明受限OS鼠标+真实图像跟随/双窗呈现+原Exit两笔文件链，不代表硬件笔或完整历史矩阵已跑。

cleanup StateMonitoring/原Mag各returned1/failed0，Product/Service/MagReady/CaptureOccupied/ownLeftHeld/logFailed全部0，进程已退出。已交独立checker复读raw/PNG/实际文件；long、Edge/Word/PDF、真实笔/触控、旧平台和生命周期现场矩阵仍未运行，不能标全功能完成。


## Long 独立入口最小增量已应用/构建，尚未运行（2026-10-23）

父会话在short独立raw gate通过后授权：以D83E9F65**当前**inl重放long-specific hunks，不使用旧6C655BF staged fullfile覆盖。实际6文件范围为当前inl、IdtMain.cpp、IdtMagnification.h、Business/ScrollFollow.cpp及ignored launcher/offline runner。before/proposed/精确long-stage.patch/各文件baseline与proposed SHA留在 `Build/ScrollFollow/product-live-stage/long-stage-1023`；生成前/应用前逐一核对baseline，BOM/纯CRLF/原EOF保持，Main无EOF newline继续保持。KnownInkEdge+ReadInkScreen、NativeMouseDownBound+ReadInkExit、CheckFixture+CheckDesktop三段逐字与D83E相同，未知像素/GT/源失败诊断和CompareInk耗时全部保留。

新严格 `--scroll-follow-live-long-test` 使用第8早退分支；全argv坏组合在普通Main前exit2，harness-off也拒绝。long专用initial horizontal+32/-16加现有vertical+32/-16，最终viewport预期(16,16)；CompareInk共享函数新增dx、保留原双向max≤2px/count90–110%。随后真实wheel以16..128px步幅至少10*主屏物理高度下行，再逆相同步长返回，用实际Tracker/channel/applied/源paint收敛及每步原99点+精确ROI验证；不手写Target、ACK或接触seam。A离屏期间不伪造非空像素要求，返程必须重见A并比较2D位移，Off/B/原Exit文件沿原链保留。规划通过选择已知A走廊不覆盖GT的端点，绝不跳过GT样点；2880×1920独立几何核验151步/每程、19200px、所有99点和精确ROI安全、逆程回(16,16)。未强制Tile budget/驱逐，返程像素不能宣称特定GPU缓存层被强制淘汰。

Business只在既有verbose逐帧同一日志行追加真实context session、frame capture_session与现有initialSurvivors/added/retained/retired，仍走唯一有界队列，verbose关闭不新增逐帧输出；没有新增public Snapshot或mutable观察API，Tracker不改。后续必须离线匹配实际session/capture_session，找到accepted且initial_survivors=0后仍继续正确跟随的真实帧，relayGeneration不作为退休证据；保留diagnostic dropped事实，不要求日志假称无损。

按父会话调整预算，long内部watchdog480000ms；短程90000ms不变。launcher默认100000ms不变，新增显式TimeoutMs（上限600000）；long预算不足500000在StartProcess之前明确拒绝，实际默认long参数只执行guard即PASS，未启动进程。正式long必须传 `-TimeoutMs 500000`，未完成/超时不能记正常drain或缩距离/跳等待。

完整原生ARM64 `product-live-long-1023` build **exit0 / 18,904ms**；原始 `Build/ScrollFollow/motion-tracker-build-product-live-long-1023*`，仅已有hashlib++/Setting/Main转换warning，没有新增long/Business warning。最小离线 `product-live-stage/offline-5882187/result.json`：原16+新增9共25坏argv全exit2/双输出0B，protocol0/229ms、state0/81ms。inl `18F4311C00267FB75ED4D2031BE11783B50084DAFDAC7E0A5E642408547143DC`，Business `C50A18FA56892319D82B92AAF76B7DA886F86207B6076F74BF7DE03B6DF9B0F7`，EXE `24D8FAE8308B605CE88ECF40D6DBC46BC20DBBA107906A3EC99BB11323A397E4`。尚未新GUI；交checker增量独立检查，放行与父会话交接后先一次原short确认，再一次long。未commit/push、未扩原保存/GPU/输入/捕获实现。


## Long build 的原 short 实屏回归 PASS（2026-10-23）

checker放行18F4311C/C50A18FA/EXE24D8FAE8后，父会话只交接一次原short入口，未自行开始long。轮次 `product-ink-long-build-short-1023-6054687`，**PID26356 / exit0 / 37,169ms**；launcher启动前EXE24D8FAE8、LastWriteUtc=2026-10-23T00:49:35.5876427Z、timeout100000。root `Build/ScrollFollow/product-live-26356-6055187`，原始 `Build/ScrollFollow/live-product-ink-long-build-short-1023-6054687-test*`。

原shared CompareInk/InkWheel新版本仍完整通过真实A→辅助pair(0,1)→vertical32→reverse16→Off pair(1,0)→真实B→原Exit。A core18722/unknown0，四次双向37444点median/max全0，比较耗时15.013/12.744/11.502/14.757ms；Capture accepted99/rejected0/API0，latest/applied99、最终viewport(0,16)、clamp0；实际窗口ACK1。B core18678/unknown0/known_edge6/maxResidual.4203；GT每条99/99及精确ROI通过，DPI192/192/1:1及原推导界通过。

真实Exit唯一序号/daily1，文件 `AutoSave/desktop/2026-10-23/085321172_7503c12c.uink`，两Pen227/228点、同身份、viewport(0,16,1)、ReadComplete/ImportSuccess，actual_exit与bounded_case真实PASS。两线程returned1/failed0，Product/Service/MagReady/CaptureOccupied/ownLeftHeld/logFailed全0，进程已退出。没有build/source修改、重试或long执行，已交独立checker raw核验，等待父会话另交long。


## Long 唯一实跑：首个128px阶跃触发三帧拒绝，未通过（2026-10-23）

父会话另行交接long，当前24D8FAE8未重建/改源。轮次 `product-ink-long-live-1023-6135484`，**PID16860 / exit1 / 50,590ms**；显式launcher timeout500000、内部watchdog480000，实际不是超时。私有root精确为 `Build/ScrollFollow/product-live-16860-6135937`，原始 `Build/ScrollFollow/live-product-ink-long-live-1023-6135484-test*`。仅运行一次；失败后没有减步、跳等待、恢复、再开窗或重试，未使用Computer Use/防锁。

实际长入口先完成A真实OS/RTS和辅助交接、vertical+32/-16以及horizontal+32/-16，最后源total(16,16)，各实际ACK1。五次A双向34486点/core17243均median/max0px（handoff、vertical32、reverse16、horizontal32、horizontalreverse16），比较10.487/11.079/11.937/13.823/11.143ms；所有已执行GT条目仍99/99+精确ROI。此为有限二维平移实屏证据。

规划日志正确记录物理height1920、单程19200px、151步/每程。刚发送**第一个128px**真实vertical wheel（delta-960），源paint6已从(272,528)到(272,656)。尚未得到第1步wheel_settled/step记录，真实Tracker连三帧拒绝而暂停：同session1/capture_session1，帧198 accepted1/None、total(-16,-16)、initial_survivors286；帧199/200/201全部accepted0/InconsistentTranslation、inliers2、fbP95.8780、coverage0、total仍(-16,-16)。Paused事件按既有三拒绝合同触发。拒绝帧initial_survivors0是该结果默认/失败统计，**不是初始点退休证据**。没有达到十屏peak、初始点全离屏后的继续跟随、返程A、正常Off后B或本case文件验收。

失败事实为Business Paused，Capture accepted201/rejected0/API0/quality0，actualACK1、pair(0,1)、latest/applied198、owner保持最后可信viewport(16,16)、clamp0，security0。由现有日志只能说明128px阶跃未获足够一致平移支持；支持其超出该纹理/当前LK条件下能力的推断，不能没有帧/位移场证据便唯一归咎PrintWindow、Mag刷新或算法某一参数。未把背景真值输入Tracker，也未容忍错误位移强行通过。

异常cleanup沿实际End并回Off，StateMonitoring/原Mag各returned1/failed0；Product/Service/MagReady/CaptureOccupied/ownLeftHeld/logFailed全0，进程已退出。Exit只捕获当前一笔A/3203bytes并commit成功，这是失败清理事实，不是本long两笔ReadInkExit PASS。诊断队列有dropped1/contention1，保留原始事实，不称日志无损。已即时报父会话和checker，源码/binary冻结，long总体未通过。


## 独立离线捕获范围诊断及 ignored LK 参数对照（2026-10-23）

父会话授权仅诊断，不改生产Tracker算法/参数、不GUI。独立控制台源及编译/产物全部在ignored `Build/ScrollFollow/texture-motion-diagnostic/`，复用已有vswhere→vcvarsall ARM64→cl方式和headless同/MT、现有OpenCV静态库；没有新项目、构建系统、包或依赖版本。初次链接明确缺gz*，补现有zs.lib后编译exit0；OpenCV header C4864保留，不改第三方。编译直接使用未改生产VisualMotionTracker.cpp，TexturePixel与原测试Canvas/Render从当前源码逐字抽取。控制台只GetMonitorInfo/SetProcessDPIAware读当前几何，不创建HWND或取屏。

当前独立主屏2880×1920、生产源矩形(0,0,2879,1919)/frame2880×1919，GetMonitorInfo当前workArea(0,48,2880,1856)，按Business规则裁ROI2880×1808；Tracker自身做原半分辨率灰度，不在诊断重复缩小。生成BGRA像素按原世界公式、起点(256,512)及真实x/y±整数偏移；原测试Canvas在同native几何对照。**没有保存上次失败Mag帧/历史workArea回执**，这些是当前受控公式/几何合成图，不等同历史截图。每个fresh幅度0/16/32/64/128/192/256，x/y正反（静止只一个），3次同位姿观察拒绝/暂停，仍Tracking才返原点；另以各8静帧复放0→(0,32)→(0,16)→(32,16)→(16,16)→(16,144)，时间分配是显式有限模型，不冒称当时201帧原时序。

baseline PID19516 exit0/18.755s，318条结果，**exit0仅诊断顺利结束，不代表所有位移通过**。controlled静止/16/32四向全接受，64四向全InconsistentTranslation（53–60FB有效/12–23内点），128四向全拒（38–45FB有效/0–3内点，正y128内点2）；original Canvas64四向接受、128四向全ForwardBackward（10–15FB有效）。两者192/256全拒；路径也在128阶段三拒绝/Paused，显示冻结LK已有可重复捕获范围不足，不是只能在Mag下出现。每条raw记录FB中位/P95、valid/forward/backward、内点、空间比/覆盖/各格、残差/仿射/次模式、参考/累计误差与耗时。

按父会话条件授权，仅ignored `pyramid4/5/VisualMotionTracker.cpp`副本将前后向两次calcOpticalFlowPyrLK maxLevel由3改4/5。production SHA `E336887415B4A0128AF4C742B1110587DED1DD2E4E83EC43991DB48BD7FFF4C0`严格核对；两副本逆替换恢复生产全部字节，header复制不改，其余FB/门槛/锚点/接受与暂停均未动。L4 PID27692 exit0/18.565s，controlled64全过但128仅水平2/4过，原Canvas128全拒，路径仍Paused。L5 PID8364 exit0/19.739s，两纹理静止及16/32/64/128/192四向全接受，含128的姿态路径均继续Tracking；controlled256仅1/4接受、original256全拒，不能称任意快滚通过。L5 accepted结果最大累计误差controlled.00021198px、original.00003052px；拒绝帧error128等是未提交位移与真值的距离，不是接受后的漂移。

全结果和参数补丁在summary.json/baseline-parsed.json/parameter-variants.json、各raw/result.json，且source-before.json所记生产cpp/h、currentinl、原testfile、24D8 EXE五hash全部不变。流程总耗时不等于实时FPS，不据不同接受/拒绝路径的flow耗时比较产品性能。建议候选仅两个LK maxLevel整数3→5，仍需父会话审证据后授权生产应用，再过既有完整Tracker静止/合法/干扰/拒绝/形变/长接力回归及本诊断，最后重做实际128阶跃/十屏GUI。当前未应用该生产候选，也未宣称历史Mag失败已修好。


## 小尺寸clamp spot 与未应用的生产候选（2026-10-23）

父会话要求L3/L5在960×540及1920×1080只测128px四向。仅ignored console加严格 `--spot w h`；这条路径不查询桌面、不创建窗，原参数/fixture保留。OpenCV同INTER_AREA半res+21×21窗口后，用现有buildOpticalFlowPyramid只读记录真实可用maxLevel：960×540的480×270处理图，requested5实际clamp3；1920×1080的960×540处理图，requested5实际clamp4。四个spot进程均exit0，raw在texture-motion-diagnostic/spot3和spot5，spot-summary.json汇总。这里exit0仍仅表示诊断完成。

960×540两纹理128四向在L3/L5均拒绝，字段结果一致；不能说提高maxLevel使任意小图都可追128。1920×1080的L5受控纹理128仅水平±接受（误差.00017263/.00003052），垂直±仍InconsistentTranslation；原Canvas128四向仍拒。此直接限定native大ROI结果的可泛化范围，也解释既有960×540多数回归实际不能建到第5层；仍须真实执行回归，不能因此跳过。

未应用的production-candidate-not-applied.patch精确只有前后向LK两处maxLevel3→5，以及一条“大屏半分辨率仍需覆盖较大位移；小图由OpenCV自动限制可用金字塔层级”中文注释。建议必要持久测试在原visual_motion_tracker_tests.cpp追加受控BGRA/nativeROI2880×1808的大步16/32/64/128/192四向与反向、128步长长程初始点退休后继续正确累计/返程；小尺寸128继续验证拒绝保留最后可信参考/第三帧Pause，不能要求其接受。原14组6918合法帧、严格prefix误差、静止.5px及所有invalid/混流/FB/形变/拒绝事务均保持。还需完整ARM64solution/Tracker-only与protocol/state/本诊断、独立check后实际short/long。当前生产cpp/h及24D8EXE仍冻结，不加phase correlation或新的初值策略，等待父会话决定。

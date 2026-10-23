# P4 final source review / short verification

2026-10-23，trellis-check `/root/p1_review`。全部 writer 释放后审查/自修复；main 仅并行维护规范、任务文档和实际 GUI 验证。没有子代理、commit、push、工具链改动，也没有从 checker 启动可见 GUI/Computer Use。本报告记录最终源码与短测试，不把 GUI 或性能待验收项写成通过。

## Findings (fixed)

- Files: `Graphics/FreezeCapture.h/.cpp`、`UI/ScrollFollowDebug/ScrollFollowDebug.cpp`、`scroll_follow_debug_tests.cpp`。
  - Issue: 以累计 accepted+rejected 推断质量分析有效性不成立。Read API 失败增加 rejected，但没有执行 Analyze，Begin/Update API 失败还会清空旧 quality；默认 InvalidBuffer 被显示成真实分析结果。
  - Fix: Snapshot 追加 `qualityObserved`。仅原 Read 结果位置按真实 `result.succeeded` 标记；与原 API quality 清零位置一起清为 false。UI/JSON 直接消费标志，原 Analyze、计时、计数、决策及调用顺序保持。扩展现有 Session/export 短断言：成功分析有效，Read API 无分析为 N/A/null，真实质量结果保留。
- Files: `Business/ScrollFollowDebug.h/.cpp`、`Business/ScrollFollow.cpp`、model/control 短测试；UI writer 同步事件显示/导出。
  - Issue: 触发暂停的原 Capture Read/API/Quality 快照可在 Stop 后被 End 失败覆盖，首因粗分类虽保留，具体阶段/时间/原生错误却丢失。
  - Fix: `NumericSummary.captureFailure` 保存事件当时纯数值；Tick 在原 Stop 前通过 transient debugOnExit channel 记录实际判定快照。geometry/unavailable 退休只在实际 stage/reason/API counter delta 证明新故障时记录；owner/Freeze/Shutdown 不伪装成捕获拒绝。dedup 比较 time/stage/native-valid/code，不以 Requested 单独 reason 变化新增故障。短断言覆盖同一时刻 Read123→Requested→Read124→End、真实 Tick Stop 前快照、几何退休不伪造 Pause/Tracker reject。
- Files: `IdtMagnification.cpp` 和 UI picker getter（与实施者协调）。
  - Issue: 独立 STA 的 IFileDialog 不是 roleDebug，单独排除 debugger HWND 不能证明选择器也被排除。
  - Fix: UI 在选择器自己的 STA 用 IOleWindow 观察并验证当前 HWND，仅原子 getter 跨线程；Show 返回、取消和 RAII 清理均清除。原 BuildCurrentFilterList 每次读取 getter，沿原 IsWindow/PID/child/dedupe 验证加入当前句柄；无新 capture/affinity/缓存表。真实选择器像素与取消/退出仍由 GUI 验证单独记录。
- Files: `IdtMagnification.cpp`，由 probe writer 修正并复核。
  - Issue: 未承诺 last-error 的 API 被标记为 nativeErrorValid=true。
  - Fix: 仅失败文档明确允许 GetLastError 的 SetLayeredWindowAttributes/GetClientRect 为 true；PrintWindow/GdiFlush/Redraw/Invalidate/MagSet/Get 维持原调用/短路及 false 有效性。PrintWindow 失败仍执行原 GdiFlush。具体主源文档见 `p3-probe-implementation.md`。
- Files: `UI/ScrollFollowDebug/ScrollFollowDebug.cpp`，部分由 UI writer 按审查反馈修正。
  - Issue: 主候选未算过的 competitor-only ratio/separation、早退 grid 分组以及 FB 失败点 fitWeight 曾被显示为已计算零。
  - Fix: main 候选为 N/A/null；cell 各组用原阶段 validity，未执行为 null/N/A；point fitWeight 要求 fbValid+Consensus。Capture 未拥有/未分析/未 Read 分别显示 N/A/null。原始 failure event 与 latest live fault 分开，native/Quality 字段按实际有效性显示。共享 writer 断言通过。
  - Issue: 新增 Space/0 功能快捷键超出批准的“不增加快捷键”。
  - Fix: 删除两条功能键分支，保留按钮/双击复位和详细面板标准滚动导航。
  - Fix: Grid toggle 的图上计数补充实际 reference/Forward，与 FB/inlier/competitor 一起按 validity 显示；没有重算生产分类。
- Files: UI cpp、`Inkeys.vcxproj`、`InkeysHeadlessTests.vcxproj`、model test。
  - Issue: 首轮完整编译 C2672：history rows 的 std::max(int,LONG) 类型冲突；两个 ScrollFollowDebug.cpp 默认生成相同目标名，MSB8027；新 byte fill 触发 C4244。
  - Fix: 改用 LONG literal，两个工程中仅 UI TU 指定唯一 `$(IntDir)ScrollFollowDebug.UI.obj`，byte fill 采用 uint8_t。没有更换源码路径、构建系统或工具链。
- Files: `IdtMagnification.cpp`、`Draw3.ProductLiveTest.inl` 的新增 debug fixture。
  - Issue: 真实 GUI 运行证明普通调试窗最初被夹具自身的全屏 Source 遮挡，尚未进入 Capture。单独跨线程 HWND_TOP 返回成功不足以证明屏幕顺序。
  - Fix: 仅夹具明确抬自己的 debug，并在 Source 自己的线程 SetWindowPos(source,debug,NOACTIVATE...) 保证相对顺序；记录前后窗口关系，保留所有 ordinary/owner-null/屏幕 GT/hit/覆盖 ROI 像素检查。产品 Open 样式与激活语义不变。产品初次开窗与真实滚轮前同样处理。main 随后真实重试确认 source_above_overlay=0，并通过完整屏幕前置与两图案覆盖 ROI 像素检查；证据见下文。产品普通非置顶、空 owner 没有改变。
  - 历史 GUI 观察按 main 要求将 debug-product 等待 35→75 秒、watchdog 150→210 秒。第六轮后用户明确选择控件交付人工验证：删除无调用的 DebugControlsHold 与75秒等待，显式记录 controls_manual_deferred_by_user=1 / interactive_controls_tested=0 / manual_entry=normal_startup；原等待后的像素检查保留为 open-before-close，直接进入真实 Close→closed15s→Off/on/off。三段各15秒性能门、全部GT/真实输入/ACK/+16和有限210秒watchdog不变，生产代码未改。

P1 threshold/comment 自修复及全部非计时结果对照见 `p1-check.md`；最终短入口又实际执行了一次。默认空 snapshot、实际短路/顺序、before/after reference identity、strongest/eligible/rejecting 三个候选角色没有改变生产算法。

## Full-scope static audit

- Three slots：producer back、UI front、atomic middle/dirty/closed 的 CAS 与 acquire/release 对换；Freeze 保留 front，生产只在另外两槽覆盖；旧 lease 在 close 后不能发布，迟到 Deactivate 不清新代次。400 帧实际双线程短压力测试完成，非手写单线程序列替代。
- 仅真实 Initialize/Process 分支构造 metadata/lease，复制实际裁剪 WorkArea 输入，保有紧凑 BGRA；错误 stride/大小/溢出/分配失败仅丢诊断。Context 不永久持有 Channel；关闭后没有图像复制、large snapshot、UI timer 或新增 Tracker timing。RecordMotion 用 try_lock，有界 30 条数值历史不保存图像，producer 不等 UI/GDI/I/O。
- 原 Target Publish/Wake/result store、Tracker 第三次拒绝及 Capture 立即暂停/退休顺序保持。AwaitTracking 的原默认 store 保留，却没有假 CV 帧/结果/拒绝。最终失败帧不会被 Paused 状态刷新冲掉，targetPublished 与 owner applied 明确分开。
- Capture completed=now 在 Backend Read 后、Analyze 前；既有 lastReadDuration 排除 Analyze，包含后端工作，并非隔离 PW 时间。Read completion 不承诺 compositor freshness。原 timer、调度和判断没有搬动。
- UI 只消费同帧 packet；processing 坐标按实际 scaleX/Y 各还原一次，anchors/delta/total 已为原图/内容坐标；未添加 DPI。Grid 为 reference voting、retained/added 为 current cells；support boxes 只是证据，green main 不等于帧接受。
- GDI backbuffer/字体/pen/brush/SaveDC/RestoreDC 配对；WM_NCDESTROY 杀 timer、释放自己的 capture、清理状态/Channel。窗口在原设置 owner 线程，但非 IsSetting 样式、空 owner、普通非置顶，不加入 overlay 链/required ready。正常关闭不退出产品也不自动重开。
- Export clone 在点击时完成；worker 不持 Channel、UiState 或产品 HWND，单个 owned std::thread、不 detach；STA 在 worker 全程有效，picker 观察和取消同 apartment，COM/GDI/数据释放后才发布 done。关窗只取消，最终 ShutdownExports 在全局资源退出前 join。只创建新子目录，不覆盖现有目录；取消/异常状态明确，可保留本次部分目录。
- 原生 API/import 静态检查：dumpbin UI/model objects 与最终 PE，和 root 暂存 P2 prototype 比较，新增硬导入为经典 GDI/text 函数和仅失败日志的 ProcessIdToSessionId（Vista 支持）。WaitOnAddress 在 P2 和最终 ARM64 PE 都已存在，不是本次新引入；不借此推断 Win7 可运行。没有 OpenCV/highgui/FFmpeg DLL、新高版本 DPI API/affinity/hotkey。IFileOpenDialog/GetTickCount64 最低 Vista，IOleWindow 为 Windows2000；[Microsoft IFileOpenDialog](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ifileopendialog)、[GetTickCount64](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-gettickcount64)、[IOleWindow](https://learn.microsoft.com/en-us/windows/win32/api/oleidl/nn-oleidl-iolewindow)。没有执行 Win7 或额外 Release/Win32/x64 构建，不声明跨平台运行通过。

## Verification

- Lint: **pass**。没有独立 C++ lint 脚本；全范围 `git diff --ignore-submodules=all --check` exit0。新增未跟踪的五个源码另逐行检查 whitespace；22 个 feature source/project 文件可解码 UTF-8、保持原 BOM、纯 CRLF，工程 XML 与唯一登记检查通过。root 维护的原 LF Markdown 未被 checker 规范化。
- TypeCheck: **pass**。使用 vswhere 动态发现 ARM64 native MSBuild；每次同一 PowerShell invocation 先 `Remove-Item Env:PATH -ErrorAction SilentlyContinue`、`MSBUILDDISABLENODEREUSE=1`，构建 `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m /nr:false /v:minimal /fl /flp:logfile=Build/ScrollFollow/p4-build.log;verbosity=normal`。按已审查 host escalation 执行，不重复已确认 sandbox stall；没有短时强制终止，等实际退出。
- 首轮 exit1，19.644 秒；C2672 首错与新目标冲突已定位修复，证据 `Build/ScrollFollow/p4-build-initial.log`。之后完整构建均成功；**含 qualityObserved/Source relative order/75s controls 的完整 solution exit0，14.580 秒，6 warnings/0 errors；其后仅 debug-product 初始夹具高度640→580并贴主屏右下边，最后加入 failure-only 数值日志与启动顺序合同的夹具修正，先完整 solution exit0，15.588秒；用户明确控件人工延期后，仅夹具收敛为非交互流程的最终完整 solution exit0，29.790秒，3 warnings/0 errors**。warnings 均是既有 hashlib++ C4267（两个 TU 各三条）。原 vcpkg `pwsh.exe` 子命令9009信息后实际回退 WindowsPowerShell 完成，MSBuild exit0；未修改工具链或项目环境绕过。
- Tests: **pass**。最终三条命令均显式 `Start-Process -PassThru -WindowStyle Hidden`、重定向 stdout/stderr、WaitForExit 并读取真实 ExitCode；cwd 为 repo。记录 `Build/ScrollFollow/p4-short-tests.json`：

| Executable / argv | Exit | 实际秒数 | 输出 |
| --- | --- | --- | --- |
| `Build/ARM64/Debug/InkeysHeadlessTests.exe --scroll-follow-debug-only` | 0 | 2.049 | CV failures=0；model failures=0；共享 export/odd mapping PASS |
| `Build/ARM64/Debug/InkeysHeadlessTests.exe --exit-visibility-gate-test` | 0 | 0.253 | offscreen debug window contract PASS；exit visibility gate PASS |
| `Build/ARM64/Debug/Inkeys.exe --scroll-follow-protocol-test` | 0 | 1.025 | actual Tick/Frame/Tracker Control failures=0；Protocol failures=0；无 HWND |

- 三条短测试实际在 qualityObserved 修正后执行；后续仅新 debug-product 夹具位置、失败日志、启动顺序合同及用户授权的非交互范围修正，未重跑无关短矩阵，该真实 GUI 入口由 main 独立重试。旧640高窗口的底部附近 GT 有3–5灰度变暗，表现与普通 DWM 阴影相符但未作因果 A/B 结论；580高并贴主屏右下边仍保留约201像素预览高度、完整256x192覆盖 ROI、所有原 GT/容差/WindowFromPoint/普通窗口合同；后续实际输入边界检查继续要求 ROI 在真实 WorkArea 裁剪输入内。只改变测试摆放，不更改产品窗口工厂或允许跳过未知像素。

- 严格错参：diagnostic/headless、protocol/product、debug-exclusion/product、debug-product/product 四前缀分别 extra/misplaced/suffix/misplaced-suffix，总计16条；最终全部 exit2、stdout=0B、stderr=0B。完整 argv 在 `Build/ScrollFollow/p4-malformed-argv.json`。**没有运行未知产品 --scroll-follow-only**，没有 --no-window、full CV/UInk/long sequence。
- Export：真实共享 writer 和既有 PNG decoder 检查 clone/source reuse、401x303→200x151 奇数各轴映射、cyan/4x green arrow/current support 独立原图像素 GT、raw 全像素不变、未执行 null/main/cell/quality/native validity、NaN/Infinity JSON、cancel 无文件。另独立解析两个 PNG header、JSON identity/48cells/error 与 cancelled 空目录；`view_image` 实际查看 raw/preview，原图纯底色、标记只在 preview。
- 最新产物目录：`D:/Project/Inkeys/Repo/Inkeys/Build/ScrollFollow/debug-export-test-41176-38500937`，文件 `raw.png`、`preview.png`、`snapshot.json`；这个目录只证明共享导出/renderer/serializer，不等于 native picker GUI。
- 导入证据：`Build/ScrollFollow/p4-product-imports.txt`、`p4-prototype-imports.txt`、`p4-ui-symbols.txt`、`p4-model-symbols.txt`。使用同一个 vswhere native dumpbin 重新生成最终文件；最后 fixture failure-only SessionId 观测引入 ProcessIdToSessionId，最低 Vista，不改变权限/会话。

- main 实际 GUI/Mag 证据（checker 只读复核日志，不启动 GUI）：`Build/ScrollFollow/live-1096-38643296/result.log`，主任务读取真实进程 ExitCode=0。screen outside=54/54、preview background=9/9、owned hit=1、uncovered ROI exact=1；两图案 matched=(11,17)、covered ROI exact=1、close cleared=1、recreate filter current=1、unmatched/old/wrongThread/API failures 均0、original pair destroyed=1。这是最终完整 UI 的实际像素排除通过；此前被遮挡/安全层的运行仍是失败或阻断，不覆写为通过。

## GUI follow-up, exact failures and startup contract

- 第四轮 `p4-product-cua-4` / pid35720，true exit1。absent15.007858s：accepted89、Capture5.930227Hz、Read mean81.202596ms；open15.007308s：accepted98、6.530152Hz、Read66.363602ms、copy1.214117ms、measured Tracker13.184795ms、display age124.951500ms、UI refresh/paint9.128886Hz、consume6.530152Hz。覆盖真实 Tracker 输入49152pixels/maxerror0、实际+32/-16滚轮都通过。数据是单机有限观察，不能声称性能提高，closed段未到达。
- 第四轮在75s观察期真实 Paused。`p4-product-cua-4.stderr.txt` 417–419行：frame350/351/352连续ForwardBackward，forward74/backward6/fbValid5，rejects1/2/3，reference349不变，可信totalY=-16。Capture accepted352/rejected0/API0，故为实际Tracker拒绝而非Capture质量/API失败。没有CUA输入，未实际覆盖控件；不把暂停归环境或新代码，也不更改算法凑通过。
- 第五轮 `p4-product-cua-5` / pid40768，true exit1。屏幕87/87、debug9/9，但 `open-before-performance` actual Tracker input sequence113的49152pixel proof maxerror225/exact0。failure-only日志：Windows Session1、已Paintseq112、liveTracking/pauseNone、CapturestageNone/rejected0/API0、Tracker3A/0R。这是实际像素检查FAIL，不能改写为环境/通过；没有走到open计时或closed。
- 为准确分离失败，新增仅 debug-product catch 路径的有界数值日志：当前SessionId有效性、已PaintProbe、ReadLive首因/Capture事件、最近6个真实Motion的reason/连续拒绝/reference身份及raw统计。raw统计明确标记summary未存阶段validity，不把未算0当有效。只读、不写图、不重跑CV、不改变任何门或生产代码。完整solution通过；新Session API最低Vista，见[Microsoft ProcessIdToSessionId](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-processidtosessionid)。
- source-only根因范围：正常 `IdtMain.cpp:3755` 只调用一次Open，早于StateMonitoring/UI3初始化线程和`MagnifierThread`（约3788）；Business默认为Off，StartCapture来自实际Toggle后的Tick。因此正式startup-only主窗口不可能与旧Update/Read重叠。旧fixture却在已有Follow/Capture的15s absent后Show普通窗口；Update才提交filter，Read另相执行，存在旧filter已准入时再Show的不同路径。**这证明可达性差异，不等于已证明225差的根因**。
- main批准的最小fixture更正现已编译：原absent段后真实RequestStop，等待Complete/handoff/final applied/unblocked、真实反向双窗ACK、primary可见和Capture publishing/occupied/inFlight均false；随后原Open，再真实RequestToggle/F0/Tracking/双窗ACK，重新取得生产Host channel和base。Host通常复用channel对象，未人为分配或写入新channel。要求新business/Capture IDs；open/closed沿同一个新会话，absent为旧会话，日志明确`absent_open_same_session=0`与`performance_causal_comparison=0`。全部GT、原始输入256x192 proof、15s窗、最后+16/真实Off/ACK及关闭后不重开不变；当时仍保留75s观察，随后用户明确取消该控件等待，见下文。没有改生产Open接口、捕获调度、算法或提供hidden-open API。后续实际运行由main记录，旧失败保留。
- 第六轮 startup-corrected `p4-product-cua-6` / pid3752，真实exit1。已复核 stdout/exit JSON：absent15.007318s accepted100/6.663416Hz/Read60.648530ms；open15.002730s accepted98/6.532145Hz/Read67.524878ms，copy1.233099ms、measured Tracker14.900657ms、age124.005933ms、refresh/paint9.131671Hz、consume6.465490Hz。真实Off/drain成功，absent business/capture=(1,1)，open=(2,2)，明确不可作跨会话性能因果比较。实际输入覆盖ROI三次各49152pixels/maxerror0，+32/-16实际滚轮/ACK通过。75s观察内 frame569–571为真实InsufficientFeatures(Forward=1)，连续拒绝1/2/3后Paused；ref568/totalY=-16保留，Capture571/rejected0/API0，首因TrackerPaused，最后已Paint seq570。没有控件输入，旧运行仍FAIL，closed未到达。主任务工具终于枚举出Diagnostics HWND7738178，但读取状态被授权拒绝，原文 `Computer Use was not approved to use inkeys`；不得猜测/绕过工具授权。
- 用户随后明确选择“控件项目交付人工验证”。本次仅删 debug-product 的75秒交互等待和无调用helper，增加显式manual-deferred日志，保留所有既定Capture/Tracker/GT/15秒性能、Close、同新会话closed段、真实Off/on/off及+16/ACK断言；不把未操作控件写成Pass。scope日志改为noninteractive，210秒watchdog不变；正常startup路径交付人工控件步骤，未来不再建议75秒交互入口。最终完整solution真实exit0/29.790s/3既有warnings/0errors，证据p4-build.log；仅fixture改动不重跑无关短矩阵，由main执行最后非交互GUI入口，最终通过详见下节。
- native picker仍是合法的active Capture期间动态可见窗口；上述主窗口startup-only事实和fixture更正不能证明picker初次可见/filter时序。该GUI项仍UNVERIFIED。
- 工具可见性只读审计：当前ARM64 PE使用既有`GenerateManifest=false`、link `/MANIFEST:NO`；没有RT_MANIFEST id1，唯一id221为PptCOM COM注册，提取在`Build/ScrollFollow/p4-product-manifest-221.xml`。正常UIAccess重启在IdtMain2295，严格CLI已在更早返回而不会执行。helper普通Start-Process无Verb/Credential/session切换。pid40768实测Session1；没有充分证据与Sky宿主Session作比较，也没有理由修改UAC/manifest。Source的ToolWindow/NoActivate不能解释普通WS_EX_APPWINDOW Debug也不在targetable inventory；工具只返回用户其它app，真实原因未明，不能猜Window对象或绕过选窗约束。

## Final noninteractive product evidence

主任务运行实际 `Build/ARM64/Debug/Inkeys.exe --scroll-follow-debug-product-test`，`p4-product-final.pid.json` 开始18:40:00.119、`p4-product-final.exit.json` 完成18:40:56.692，pid27944真实ExitCode=0。checker只读复核stdout并独立断言三段、四次真实输入、滚轮、关闭及cleanup；没有启动GUI或输入控件。证据 `Build/ScrollFollow/p4-product-final.stdout.txt`、`.stderr.txt`、`.exit.json` 和 `product-live-27944-41287078/result.log`。

| Phase | 秒数 | accepted / Capture Hz | Read mean/max ms | Tracker accepted/rejected |
| --- | --- | --- | --- | --- |
| absent | 15.009097 | 92 / 6.129616 | 79.280935 / 123.270000 | 92 / 0 |
| open | 15.000626 | 94 / 6.266405 | 73.556649 / 108.308000 | 94 / 0 |
| closed | 15.006483 | 101 / 6.730424 | 63.582089 / 88.404000 | 101 / 0 |

- 三段Capture rejected/API为0，阶段内真实会话稳定。absent business/capture=(1,1)，先真实Off/反向ACK/drain，再原Open和真实Toggle/F0/双窗ACK，open/closed=(2,2)；absent不同会话，日志明确`performance_causal_comparison=0`，这些有限数值不能证明诊断开关导致性能增减。Read含后端工作而排除Analyze，非隔离PW或合成器freshness保证。
- open预览85个独立display样本，copy均值1.225421ms、measured Tracker total15.350918ms、age157.163300ms、refresh/paint9.132952Hz、consume6.266405Hz，published94/overwritten0；closed刷新/复制/发布观测为0。资源边界同日志：open private623341568→624644096B、GDI121→120、USER81→80；closed private560037888→560103424B、GDI117→117、USER78→78；只作为有限现场观测，不作泄漏或回归因果结论。
- 屏幕GT在open维持87/87、诊断实际可见9/9；闭窗恢复全99/99。实际Tracker输入覆盖ROI四次（open前、+32后、反16后、关闭前）各49152pixels，maxerror0/exact1；原始容差/全部GT未放宽。真实带tag系统滚轮正32/反16均实际ACK，最终source/viewport=(0,16)。
- 真实Close销毁HWND、disabled publisher、旧channel和storage释放成功，closed段保持同Follow会话2；原真实Off、再次On再Off均双窗ACK/捕获drain，automatic_reopen=0。stdout最终bounded_case=PASS；state/mag线程均returned1/failed0，product/service/mag_ready/occupied/own_left均0，log_failed0、cleanup result0。
- 日志显式`controls_manual_deferred_by_user=1 interactive_controls_tested=0 manual_entry=normal_startup`。本次通过范围为非交互性能/像素/生命周期，不等于Freeze、标记/缩放、native picker、取消/退出等控件GUI全通过；这些按用户新范围交付正常startup路径人工验证。此前第四/第五/第六轮真实失败保留，不追溯改成Pass。

## Findings (not fixed) / remaining evidence

静态范围没有剩余已确认的生产代码缺陷；实际产品验证存在上述失败，不能因为静态/短测通过而认定完成。以下明确不是通过项：

- 用户明确将 native picker/取消/关闭、Freeze/恢复/标记/缩放等控件项目交付正常startup路径人工验证。最终完整UI的实际Mag像素排除及close/recreate已有上文通过证据；非交互产品三段性能/生命周期已由main独立运行并通过，证据见上节；其范围不能推断人工控件已通过。保留第四/第五/第六轮实际FAIL。本checker未运行可见GUI，未操作的控件保持UNVERIFIED。
- 没有注入真实 OOM、实际 Win32/COM/文件系统运行期故障；共享 writer 的取消前零文件通过不等于对话框取消/关窗/进程退出的现场证据。
- Forward 低点数、JumpTooLarge、AnchorDisagreement、OpenCV/其它异常没有专门新增算法负例，遵 main 要求仅静态核对原分支，不扩张短矩阵。
- Win7 与非当前架构/Release 运行未验证；静态兼容审查不能代替这些平台证据。

本报告交接最终代码、成功完整编译和短验证；不宣称整个 GUI 验收完成。

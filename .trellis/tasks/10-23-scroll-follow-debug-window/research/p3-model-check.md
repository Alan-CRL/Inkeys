# P3 model / Capture / Business 独立审查

2026-10-23，基于任务批准的 feature/move 980dc39b。当前阶段仅做静态审查；main 尚未释放 UI/probe writer，明确禁止构建和运行测试。这里只记录实际检查，不能把已写测试当作通过。后续完整 review/构建结果将继续更新本报告。

## 归属与范围

- 已加载最新 check.jsonl、prd/design/implement、native desktop 规范、scroll-follow-debug-window.md、p3-contract.md 与 p3-model-implementation.md；旧规范未变部分沿前一轮已读上下文。
- 当前归属：Business/ScrollFollowDebug.h/.cpp、Business/ScrollFollow.cpp/.h、Graphics/FreezeCapture.h/.cpp、Headless model/严格短入口及必要项目注册。
- UI/ScrollFollowDebug、IdtMain、IdtMagnification、ProductLiveTest 仍由其它 writer 持有；只读相关接入，不修改或回滚其内容。本阶段没有代码修改，无 commit/push。

## Findings (fixed)

归属范围暂未发现需要自修复的已确认代码缺陷。

## Findings (fixed，原生 backend 交接复核)

- File: `Inkeys/IdtMagnification.cpp`（probe writer 归属）
  - Issue: 部分 API 失败将随即取得的 GetLastError 一律标为 nativeErrorValid=true，但对应官方接口没有承诺设置 last-error，无法证明该值属于这次失败；立即读取只能防止后续覆盖，不能证明它不是更早残留值。
  - 证据：检查时 Read 的 PrintWindow/GdiFlush 失败返回统一 true；RedrawWindow/InvalidateRect 与多个 Mag setter/getter 也有同类声明。GetLastError 官方说明明确区分文档未承诺设置 last-error 的情况。
  - Fix: main 接受问题，由 probe writer 在其归属内修正；释放后已复核实际 diff。仅文档承诺错误码的 SetLayeredWindowAttributes/GetClientRect 在失败位置立即取 GetLastError 并 valid=true；PW/GdiFlush/Redraw/Invalidate/Mag setter/getter 与包装/几何值失配均保持默认0/false。原 PW 后 GdiFlush、Update 短路与几何决定不变。默认0仅表示无有效错误码，不宣称操作成功。
  - Primary sources: [GetLastError](https://learn.microsoft.com/windows/desktop/api/errhandlingapi/nf-errhandlingapi-getlasterror)、[GdiFlush](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-gdiflush)、[PrintWindow](https://learn.microsoft.com/en-us/windows/desktop/api/winuser/nf-winuser-printwindow)、[RedrawWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-redrawwindow)、[InvalidateRect](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-invalidaterect)、[MagSetWindowFilterList](https://learn.microsoft.com/en-us/windows/win32/api/magnification/nf-magnification-magsetwindowfilterlist)、[MagSetWindowSource](https://learn.microsoft.com/en-us/windows/win32/api/magnification/nf-magnification-magsetwindowsource)、[MagGetWindowTransform](https://learn.microsoft.com/en-us/windows/win32/api/magnification/nf-magnification-maggetwindowtransform)。这里没有声称这些 API 在本机一定留下旧值；问题是原 valid=true 超出已确认的错误码合同。

## 三槽 / 寿命静态检查

- 初始 front=0/back=1/middle=2 三槽不同。producer 只访问 back，consumer 只持有 front；双方 acquire/release CAS 交换 middle。consumer 把旧 front 放回 middle 后，后续只访问新 front。producer 仅在成功发布后取得旧 middle 作为 back。额外固定数组和图像不放进 Context。
- Closed 与 dirty/index 使用同一个 packed middle。Deactivate 的 fetch_or 和 Publish/Consume 的 CAS 在线性顺序上互斥；关门后旧 lease 只能失败，无法清掉 closed 或写到新 Channel。迟到旧 Deactivate 使用 shared_ptr expected 身份，不会撤掉新代次。
- BeginFrame 的 borrowed BGRA 每行复制为紧凑自有 vector；先核正数、BGRA、64M pixels、stride、乘加溢出与可读最后行。分配/布局失败返回空 lease，只增加诊断丢帧，不改变真实 Tracker 输入。异常只在诊断局部捕获。
- 暂停/Freeze 不再消费 front 时，生产者仍在另外两槽交替。CloneDisplayed 由唯一 UI owner 在稳定 front 上深拷贝，不把 Channel 借用或原 Capture span 交给 worker。
- WriteLease move/reset 保留唯一写入归属，未 Publish 的 lease 只释放对应 writing 标志和寿命。关闭期间的 callback/lease 保活旧存储至最后引用释放；Context 只存小 atomic 首因，不固定大缓冲。
- RecordMotion 的数值 ring 使用 try_lock；竞争只丢摘要，不阻塞 Mag 等待 UI。ReadLive/UpdateStatus 在短数值锁内只处理固定值，没有图像、GDI、目录或 I/O。UpdateStatus 由 controller 执行，不能作为新图像发布。

## 业务接入 / 首因静态检查

- Frame 原 owner/session/capture/paused guard、ROI 算法与真实 Tracker 参数保持。prepareDebug 仅位于实际 Initialize/Process 分支；AwaitTracking 保留既有默认 result store，但不 BeginFrame/RecordMotion，不增加 trackerRejected。
- metadata 的 geometry/quality/statistics/timestamp/interval/ROI 只来自回调同一个 Capture::Frame，windowGeneration 来自当前 Channel，businessSession 来自实际 Context；没有调用新的全局 Snapshot 拼成同帧数据。
- 原 Target Publish、WakeHost、context 及 resultMutex store 的顺序保持。targetPublished 精确记录 Publish bool，不当作 owner 已应用。RecordMotion 在 Publish lease 之前读取该 back 的同帧 copyMs，包与摘要使用同一 totalTrackerMs。
- totalTrackerMs 的两个 Clock::now 只在取得 Channel 时执行；起点在 BeginFrame 复制后，终点在真实 CV 返回后/Target 发布前。不把四个分段相加当总时长；关闭路径不新增这些计时或大型快照。
- ROI、TrackerPaused、CaptureQuality/API/Timeout/Unavailable、clamp、初始化超时和控制异常在原业务故障点 CAS(None,cause)。后继 Stop(Requested) 或 Paused Tick 的 !publishing 无法覆盖已有首因。不为 Initialize 返回的 CV Paused 新增业务暂停行为。
- controller 以匹配 captureSession 的快照生成 LiveStatus，ownerAppliedSequence/Viewport 来自 FollowChannel::Read 的实际值。状态与历史变化不触碰 front/pending，最终失败图像不会被默认 MotionResult 覆盖。

## Capture 只读补充静态检查

- 与 HEAD 对照：Begin/Update/Read、Stop、Drain、Pump/监督的原调度、quality 分支、重试/截止/accepted 与 lateResults 决策均保持。新增字段只是原成功/失败位置赋值。
- 精确时序：原 `completed = now_()` 在 Backend::Read 返回后、Analyze 前。lastReadDuration 仍为 completed-began，涵盖整个 Backend Read，但不含 Analyze；lastReadCompleted/timestamp 也不是 compositor 新内容凭据。main 已纠正旧研究及规范中的相反说法，本 reviewer 不挪动计时。
- Read 开始/结束与完成间隔在原 Read 统计位置填；lastAcceptedSample 只在真实 accepted 分支推进。API 失败保留原失败 phase 与 BackendResult native 有效性，quality/Callback/End/Timeout 使用自身真实位置且没有假原生错误。
- Requested Stop 不更新 failureStage/nativeError；后续真实 End 故障可以成为“最近捕获故障”，业务 Context 首因仍保持原值。

## 已写测试审查（尚未运行）

- Model：实际 padded/odd/nonzero ROI 像素、借用源复用、导出 clone、Freeze/latest-only、30 条摘要、最终拒绝帧保持、无 CV 状态无假拒绝、错误布局、旧 lease 关闭/新代次、weak_ptr 回收、两个线程竞争首因。
- 400 tiny frames 的两个实际线程，consumer 校验每个已见包的完整像素/sequence/Tracker 点身份且最终到400；原子 finished/invalid 避免测试计数竞争，producer 不调用非原子的 Check。
- Session fake backend：实际 Read 时间/间隔、成功/Read API/quality/Requested/Begin/Update/Callback/End/Timeout 及原计数/重试结果，未注入真实 OOM 或本机 API 故障。
- 真实 Business 无 HWND 的现有控制 CLI 新增 F0 ROI/AwaitTracking/第三拒绝终态/quality/API/clamp/invalid ROI 断言；只替换既有外部 Host/Capture/ACK seam，不手写 CV/目标结果。这与纯 model latch 用例分别记录，不能冒充真实 Mag 或窗口 ACK。
- animation_tests 的新入口仍全 argv 严格单参，错参直接2；不会调用完整长 CV 或 UInk 矩阵。RunExportTests 的实现/交互范围归 UI writer，等待完整复核。

## Verification（当前）

- Lint: 静态 diff whitespace pass（归属已跟踪源码/项目范围，exit0），未新增独立构建系统或 lint 工具。
- 文件检查: pass，11 个归属源码/项目均为纯 CRLF/UTF-8，已有文件 BOM 与 HEAD 相同，新 model/test 文件无 BOM；三个项目/filters XML 可解析、没有重复 ClCompile/ClInclude 注册。
- TypeCheck: **pending**，main 禁止其它 writer 尚未释放时构建。
- Tests: **pending**，本阶段未启动任何产品/Headless/GUI 测试。

## Probe / Main 扩展静态审查（writer 释放后）

- 已读 `p3-probe-implementation.md`。这四文件已释放给 reviewer；UI writer 尚未释放，仍不构建/运行。
- Main 新 flag 在现有内部 early dispatcher 枚举中追加，遍历 std::size(flags)；harness-disabled/malformed/extra argv 仍2，DPI/安全门沿原入口。普通启动只在必需窗口/Host 成功后调用一次 optional Open；正常退出在 WindowService 归并后、RenderPipeline/Display 销毁前调用 ShutdownExports。
- 最小排除夹具调用真实生产 Open/ReadProbe，不复建另一 renderer。专用 Source 降至普通 z-band，调试窗不被提升为 topmost；真实 preview 背景和 WindowFromPoint 证明可见，窗外已知 GT 与原精确源 ROI 继续核验。正式 Mag 原 worker 的被覆盖256x192 ROI 必须匹配底图，至少两图案各三帧。只有精确通过的专用 ROI 可保存；关闭/重建重新读 Service/过滤，不把列表成员当像素证明。
- 产品 probe 复用同一个实际 Follow/Capture session、正式 callback、真实窗口 ACK/正常 Off。15秒 absent/open/closed 之间各独立1秒热身；同一数值观察器贯穿三个阶段，无 MotionTrace 或图像记录。开放窗口性能阶段结束后，真实 tagged +32/-16 滚轮在窗口仍可见时经过自身 Source 命中/前台检查，最后位移仍+16，原最终工具/content/viewport 与滚轮数量断言保留。
- 窗口内35秒控件观察与计时阶段分开；冻结只读检查 generation/business/capture/frame/reference 不变且 capture 继续。未观察的按钮/导出明确 UNVERIFIED，不用只看到新帧冒充完整 latest-only 证明。三阶段 Capture FPS 按原 accepted 增量/真实 elapsed，UI刷新/消费/绘制另列，Capture Read 正确标 Analyze_included=0。
- Close 使用真正 WM_CLOSE/Service Destroy，并等待 HWND/Enabled 与旧 Channel weak_ptr 释放；闭窗阶段恢复所有源GT点。正常 Off/on/off 检查没有自动重开，不新增 Tracker/Target/viewport/ACK mutator。
- 专用 watchdog 只对新 debuggerProbe 设150秒，默认短90秒与长480秒不变；触发终止为失败，不能当正常清理证据。安全界面初始化前/等待期间仍由原门停止；actual GUI/像素/性能尚未运行，不写通过。

## Findings (not fixed，当前)

静态范围无剩余已确认代码缺陷。UI/导出完整实现及所有 TypeCheck/运行验证仍待 main 释放，不作为已完成。OOM、真实 API 失败注入和 Rejected/Paused GUI 的专门现场覆盖尚未执行。

## 确认路径后的自修复与合同交接（仍未构建）

- Files: `Business/ScrollFollow.cpp`、`Business/ScrollFollowDebug.h/.cpp`、`scroll_follow_debug_tests.cpp`。
  - Issue: 原 Tick 读取 Read/API/Quality 故障后 StopCapture，原 Mag worker 的后继 End 失败可以在退出阶段重新读取 Snapshot 之前覆盖“最近故障”。Context 的粗 PauseCause 虽保留，但旧原生错误/阶段/时刻没有按事件保存，无法恢复真正触发业务动作的数据。
  - Fix: NumericSummary 追加纯数值 `CaptureFailureInfo captureFailure`（valid/stage/reason/at/quality/nativeError/nativeErrorValid）。UpdateStatus 记录实际 CaptureFailure 事件当时值。Tick 在原暂停分支 Stop 前，通过已有短暂 debugOnExit.channel 发布本次判定所用的原 Capture Snapshot；退出析构仍发布 fresh live/latest，二者分开。既有 geometry/unavailable 退休分支仅在 actual backend stage/reason 与 API counter delta 均证明新故障时同样记录，owner/Freeze/Shutdown 退休不冒充捕获/Tracker 拒绝。
  - Event dedup 同时比较故障时间、stage、nativeErrorValid 与有效 code；不以 Stop(Requested) 单独 reason 变化增加故障。短 model 断言同一时刻 Read123→Requested-only→Read124→End，原事件不变且新真实身份有新事件；实际 Business 控制断言读取到 stop 前 reason=None 与 native123，随后 End 快照不覆盖旧事件。另用最小 geometry retire case 证明正常退休语义和 Tracker rejected=0 保持。
- Files: `IdtMagnification.cpp`、UI 的 picker getter（与原 UI writer 协调）。
  - Issue: IFileDialog::Show(nullptr) 创建独立可见窗口，它不是 roleDebug，也没有可依赖的 popup-owner 级联排除保证，可能进入正式 Mag 输入。
  - Fix: UI writer 在 picker 自己的 STA 通过 IOleWindow 观察当前 HWND、验证本进程/本线程/top-level 后原子发布，返回/取消/清理均清除；Mag 的原 BuildCurrentFilterList 每次读取 `ExportPickerHandle()`，加入同一个 IsWindow/PID/child/dedupe 过滤器。不新增捕获、亲和属性或 Window Service 架构。picker 的真实屏幕像素排除仍未运行，不因该静态修复写通过。
- Files: UI/ScrollFollowDebug（仅由仍持有文件的 UI writer 修改）。
  - 审查发现 MAIN 的 originalCellRatio/separation 并未在生产计算却被显示/导出为0；已交接改为 main 的 N/A/null，不额外跑 ratio 计算。
  - 审查发现早退时 grid 各阶段数值一直显示默认0，旧 FB 失败点的 fitWeight 也可能被当作已算；已交接使用既有阶段/FB 标志显示 N/A 和 JSON null/validity。待 UI 最终释放后复核，不抢写。
- 严格命令纠错：模型实施报告一度建议 `Inkeys.exe --scroll-follow-only`，实际 early dispatcher 没有该 product flag。已查代码并通知 main 修正文档，未执行错误命令。实际无 HWND Business 验证必须为 `Inkeys.exe --scroll-follow-protocol-test`（Protocol+Control）；Headless 的 --scroll-follow-only 是另一入口。

## 最终交接补充

上述“尚未构建/待 UI 释放”为阶段记录，已由最终 p4-check.md 取代。全部 source 释放后完整 solution Debug ARM64 exit0，三组短测试与16条严格错参均通过。新增 Capture qualityObserved 的实际 Analyze 有效性、原始 CaptureFailureInfo 事件和 actual Business Stop 前快照断言也已实际通过；最终命令、退出码、时间、导出目录及未验证项见 p4-check.md。主任务另已完成最终完整 UI 真实 Mag 像素排除，具体日志单独记录，不把该结果外推到 native picker/全部控件/性能。

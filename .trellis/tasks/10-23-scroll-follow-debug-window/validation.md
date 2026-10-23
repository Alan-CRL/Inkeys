# 验证记录（实现完成，含人工待验）

## 最终结论

- 完整 Debug|ARM64 solution exit0（29.790秒、3条既有warning/0 errors）；3个短测试、16组严格错参、22个源码/工程格式/XML及diff检查通过。
- 最终无交互产品 pid27944，真实exit0：Build/ScrollFollow/p4-product-final.exit.json。三段15秒、4次49152pixel/max_error0、正32/反16、真实ACK/Off/drain、close释放及Off/on/off不重开全部通过。Capture/Tracker rejected及API均0，最终全部worker/service/capture清理完成。
- Capture FPS未开/打开/关闭后=6.130/6.266/6.730；打开copy1.225ms、总Tracker15.351ms（显示packet样本）、显示年龄157.163ms、Preview9.133Hz。absent使用(1,1)，open/closed(2,2)，仅有限非因果观察。完整表与原始路径见delivery.md。
- 用户明确将控件交付人工。工具拒绝读取Inkeys，未执行控件输入；Freeze等实际操作、选择器首次可见像素/取消/关闭中导出、实际墨迹/定格/Edge/Word/硬件笔等待人工。最小P2原型单独像素测试未验证；最终完整UI已经过真实像素检查。
- 旧动态Open像素FAIL和两次真实Tracker三连拒绝均保留。启动顺序范围修正不证明旧225差根因，不追溯改判。无commit/push/PR，任务保留待审。

以下为执行过程记录，历史“实施中/未执行”描述不替代上述最终结果。

## P1

- 可选快照与正常调用的全部 30 个非计时统计及结果精确比较通过。严格 `--scroll-follow-debug-only` 短矩阵通过；独立审查和门限来源修正见 research/p1-check.md。
- ARM64 solution 定向测试 target 构建 exit0，短矩阵 exit0；未运行原完整长序列或 UInk 保存矩阵。

## P2

- 完整 `InkeysRepo.sln Debug | ARM64` 最终增量构建 exit0；窗口屏幕外合同 exit0，严格错参 exit2/零输出。详见 research/p2-implementation.md。
- 沙箱首次完整构建约 10:01 后 exit1、0 编译错误，主程序未更新；同一原生命令在受审查宿主路径构建成功。属于 runner/环境边界，未改工具链或源码绕过。
- 2026-10-23 实屏短入口实际尝试在初始化前输出 `[FreezeCaptureLive] blocked=security-interface-before-initialization`，未创建专用窗口、未获取真实 Mag 排除像素；**未验证**。保留安全门，等待桌面解锁状态；按已批准的环境限制交付路径继续实现。
- 该次 PowerShell 直接调用 GUI 子系统 EXE 未等待结束，`$LASTEXITCODE` 为空，外层 exit0 不能代表产品测试成功；后续用显式 process wait/ExitCode。不得将该尝试记为 PASS。

## P3/P4

实施中；完整界面像素排除、GUI 操作和有限三阶段性能对照尚未执行。无窗口导出写入验证也不能替代目录选择/可见界面的验收。

计时核对修正：Pump 在 Backend Read 返回后立即取 completed，然后才 Analyze；lastReadDuration 不含 Analyze。早先研究结论已更正，生产计时边界未移动。

派发静态核对修正：产品 Business 短入口是 --scroll-follow-protocol-test（Protocol + Control），不是 --scroll-follow-only。错误候选命令从未执行，相关研究/人工步骤已改正；避免未知参数误入普通 GUI。

2026-10-23 用户确认已解锁后，保存已构建 P2 最小原型至 Build/ScrollFollow/p2-prototype-20261023-093956/Inkeys.exe，并用 Start-Process/WaitForExit 得到真实 exit1。记录 Build/ScrollFollow/live-25436-37718093/result.log。窗口已创建，但专用 Source 在其上方：outside54/54、blue0/9、owned_hit0、uncovered_roi_exact1；Capture attempts=0。失败是夹具可见性前提，尚无 Mag 排除像素证据。已交 checker 在专用夹具中显式 HWND_TOP|NOACTIVATE 抬本进程调试窗，产品样式/owner 不变；完整 UI 待最终构建后重验。

2026-10-23 完整 UI 最终构建的再次排除尝试：进程36020真实 exit1（5.606s），记录 Build/ScrollFollow/live-36020-38073687/result.log。夹具中的 HWND_TOP|NOACTIVATE 已成功，但随后仍 source_above_overlay=1、background0/9、owned_hit0；painted1、outside54/54、uncovered_roi_exact1。Capture attempts=0，仍是实屏可见性前提失败，不能记 Mag 像素失败或通过。checker 正在定位专用源与异线程窗口的层级时序；产品非置顶/空 owner 规则不变。

共享导出 writer 的两个 401x303 PNG 已由 main 实际查看：raw 无标记，preview 的支持范围、绿色 flow 与青色补点在同一完整输入上显示；该视觉核对只覆盖离线 renderer/导出，不替代真实 GUI 或选择器。

## 最终源码离线检查与完整 UI 实屏排除

- 最终质量有效性修正：Capture::Snapshot::qualityObserved 只在实际 Analyze 后有效，Read API/Update API 失败不把默认 Quality 当已分析 InvalidBuffer；短 Session 与 writer 断言通过。
- 最终完整 InkeysRepo.sln Debug|ARM64，vswhere ARM64 原生 MSBuild、同 invocation PATH workaround、禁节点复用，exit0，14.580s，0 errors；6条既有 hashlib warning，另有既有 vcpkg pwsh.exe 环境消息，未修改依赖/工具链。
- 三个短入口重跑：Headless --scroll-follow-debug-only exit0 2.049s；Headless --exit-visibility-gate-test exit0 0.253s；产品 --scroll-follow-protocol-test exit0 1.025s。16组畸形 argv exit2、stdout/stderr各0字节；未运行长 OpenCV/UInk。
- 完整 UI 实屏排除 PASS：pid1096，监视器 WaitForExit 得到真实 exit0，Build/ScrollFollow/p4-exclusion-cua.exit.json；结果 Build/ScrollFollow/live-1096-38643296/result.log。屏幕 outside54/54、背景9/9、owned_client_hit1、未覆盖ROI逐像素1；两阶段11/17张正式 Mag callback匹配，覆盖256x192 ROI逐像素1（沿原容差3），无 callback 注册/WGC/affinity；unmatched/old/wrongThread/api均0。关闭清句柄、重建使用当前过滤句柄通过，原Mag窗口对销毁。
- 该次是最终完整 UI，没有追溯把先前 P2 最小原型被拦截/遮挡的尝试写为通过。初次初始化 GDI/USER 11->23/4->5 是有限冷启动观测，不能据此声称零缓存或长期无泄漏；重复生命周期资源合同另由短测试覆盖。
- 此前 pid34112 失败由外部前台应用遮挡，source_behind_debug1/source_above_overlay0 已证实相对层级修正正确，Capture attempts0；没有关闭/改变外部应用。改用隐藏的独立退出码监视器启动后自然通过，尚未进行 CUA 输入。

产品夹具首次完整尝试 pid41524（Build/ScrollFollow/p4-product-cua.exit.json）真实exit1。absent阶段完成15.004s、89 accepted/0 rejected/API、5.932fps；随后 open-before-performance 已实际9/9诊断顶栏像素，但87个窗外GT中4点在窗口下方y746偏暗3–5，命中均是自有Source。该位置与普通窗口阴影一致但未A/B证实，不归因Tracker/Mag；没有放宽容差。仅缩小专用夹具窗口高度以避开这些现有采样点，同时保留256×192覆盖ROI/所有GT，重新构建后重跑。首段数值仅是失败运行的部分观察，不冒充完整三段对照。所有线程/窗口按正常清理结束。

## 产品实屏后续证据与合同范围

- pid38624：缩短夹具窗口后只余窗下1个灰度差（210vs211），仍严格失败，没有放宽容差；最终夹具改贴右下角后原87个窗外样本全部吻合。该位置调整不改生产窗口样式/owner/算法。
- pid36652：第三次产品尝试初始化前 security-before-initialization、真实exit4、无窗口。用户随后再次明确确认解锁，才继续。
- pid35720，Build/ScrollFollow/p4-product-cua-4.*：真实exit1。窗口贴角后窗外87/87、顶栏9/9、三次实际Tracker输入覆盖ROI均49152 pixels且max_error0；真实tagged wheel正32/反16、原双窗ACK通过。absent/open各15秒完成。控件观察期frame350/351/352分别真实ForwardBackward拒绝1/2/3（forward74/backward6/fbValid5），可信参考349、totalY=-16保持，Capture accepted352/rejected0/API0。来源是Tracker拒绝，不能归为Capture Failure，也不能仅凭此断言输入变化的外部原因。
- 该观察期root仅调用正式Sky的list_windows/list_apps，均未返回测试Source或普通WS_EX_APPWINDOW诊断窗，因此没有合法目标可点击；没有发送任何CUA输入。窗口不可枚举原因未证实，不能凭猜测改UAC/manifest/系统权限。Freeze/缩放/标记/目录选择的真实操作仍未验证。
- pid40768，Build/ScrollFollow/p4-product-cua-5.*：真实exit1。screen87/87+9/9通过，但动态Open后的输入seq113覆盖ROI max_error225，实际像素检查失败。failure helper记录Windows SessionId1、liveTracking/pauseNone、3个Tracker接受/0拒绝、已Paint seq112；不能伪造为Paused或环境失败。此结果保留，不追溯为PASS。
- 源码核对：正常IdtMain的Open在StateMonitoring/UI初始化及MagnifierThread/Pump启动前，Business初始Off；正式使用没有在已有Capture中动态开主诊断窗的入口。动态夹具与正常启动的可达性不同，这是代码事实；它本身不证明225差的具体根因。夹具将改为真实Off/反向ACK/drain后Open，再真实toggle/F0/ACK，遵循无在途Capture前置。absent与open/closed将使用不同session，性能不可作因果推断。
- 导出目录选择器仍会在活跃捕获中动态出现，首次可见的实际像素排除/取消/退出未测，不能用启动前开主窗口的通过证据代替。没有改变原捕获调度、质量判定、滤除角色或原算法来迎合测试。

pid3752（p4-product-cua-6）遵守Off/drain→Open→新toggle的启动前置，真实覆盖像素/absent/open两阶段及正反滚动通过；75秒观察期seq569–571真实InsufficientFeatures（Forward1）第三次Paused，可信参考568/totalY=-16保持，Captureaccepted571/rejected0/API0。已Paint Probe为570，不能声称最终571已绘制。failure_live首因TrackerPaused，未伪造Capture故障。真实exit1，正常清理所有线程/窗口；不改算法以抹掉实际拒绝。

用户随后明确选择“控件项目交付人工验证”。本次Sky已列出诊断窗口，get_window_state却返回Computer Use was not approved to use inkeys；拒绝来自应用工具授权，没有取得截图/发送控件输入。后续不再使用CUA。仅去掉短产品入口的75秒人工等待，明确controls_manual_deferred_by_user=1、interactive_controls_tested=0，直接按原真实close/closed15s/Off-on-off验证；全部性能/GT/真实ACK门保留。

最终非交互 run：Build/ScrollFollow/p4-product-final.*，pid27944/exit0。absent/open/closed实际15.009097/15.000626/15.006483秒、accepted92/94/101、CaptureFPS6.129616/6.266405/6.730424，所有Capture rejected/API与Tracker rejected为0。四次实际Tracker输入覆盖ROI各49152pixels，maxerror0；startup前置、会话差异及手工控件延期均写入日志。close实际销毁、publisher_disabled/old_channel_released/old_channel_storage_released均1，closed沿用follow2；最终Off/on/off无自动重开，源累计+16/Viewport+16，所有线程/服务/占用/input归还检查通过。原始数值摘要Build/ScrollFollow/p4-performance-summary.json；最终报告delivery.md。

## 用户确认后的 TOPMOST 调整

2026-10-23 用户确认实现，明确将诊断窗口改为TOPMOST并授权提交本任务。此决定覆盖此前非置顶约束；owner=null、设置owner线程、关闭不重开及Mag排除保持。历史非置顶实屏证据不追溯为新样式验收，TOPMOST补充结果另记；控件仍按用户选择人工验证。

## TOPMOST 最终补充

用户确认后诊断窗口改为独立TOPMOST，owner=null。完整Debug|ARM64 solution构建exit0（16.392秒，3条既有warning/0errors）；诊断短矩阵exit0（1.881秒），屏幕外窗口合同exit0（0.263秒），包含TOPMOST、空owner、overlay降级/恢复、隐藏再显示、关闭重建及退出。未打开GUI；此前实屏和性能是非置顶版本，不追溯为新样式的GUI验收。详见research/topmost-check.md。用户已授权提交本次全部改动，不push。

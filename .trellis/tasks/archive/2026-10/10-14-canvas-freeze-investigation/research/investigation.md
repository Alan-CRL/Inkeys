# 画布卡死调查报告

> 2026-10-15第二轮更新：已按用户要求扩大审计，并验证三条具体停滞触发器。最新结论见 [扩大调查报告](expanded-investigation.md)。下文保留第一轮证据和当时限制；“待解锁”是历史状态，第二轮已完成有效HEAD GUI实验。用户现场共同根因仍未确认。

日期：2026-10-14 至 2026-10-15，Asia/Shanghai。任务：10-14-canvas-freeze-investigation。源码基线：bugfix/draw3 / 5d319bbf5c520dec97dcb37ad2fcc7e26d029bbb。

## 结论

故障调查已收敛到“实际笔画输入链”这一优先范围，但还没有证据确认最后一级根因。不能宣称已定位某个死锁、已经修复、或当前版本不再复现。

视频约50秒后，用户拖动鼠标不能新增笔迹，笔/橡皮工具可以切换；Win32 cursor反馈和Draw3帧日志仍在推进，旧墨迹保留，末段点击清空后旧墨迹从画面消失。这组证据更支持先检查 RTS callback、decoder/producer、controller准入这三个相邻边界。输入反馈活着不等于真实墨迹Down已经到达；空dirty的present success=0不等于GPU失败。

本轮未修改产品代码、未实施行为修复，也没有commit或push。新增内容只在本任务目录：需求/设计/执行计划、源码与视频研究、原生Win32采集脚本；生成帧图、运行副本和构建日志位于Build/CanvasFreezeInvestigation。

## 直接确认的代码事实

- 鼠标、笔、触摸的实际墨迹统一来自RTS：WindowControl的WM_MOUSE分支只发布cursor sample；RealtimeStylus::StylusDown才创建contact并发布Down。
- Setting Show/Hide没有直接disable或rebind Drawpad RTS；Explorer入口仅ShellExecute外部explorer.exe。
- Setting与Drawpad分属不同窗口线程；绘制模式建立owner关系、选择模式解除，会进入Windows跨线程input queue边界，但没有证据证明这必然造成RTS断流。
- 当前SyncDraw3State持ImGui锁同步等HWND的旧闭环已有defer guard。另有CreateSwapChain/Present在ImGui锁内的静态等待风险，但需要匹配失败现场的线程栈；不能套用到本视频。
- 现有Cursor日志可区分Down arrival/no-decoder/published，缺少Enabled/Disabled/Mapping等lifecycle阶段与外部可读writer状态；普通运行的metrics全零不能证明没有输入。
- RTS writer/decoder恢复逻辑未发现一个可确认的必现自锁。shared-scale基准context失败牵连其它设备是一个具体候选，尚需实际HRESULT和tcid验证。

带行号的完整调用链：canvas-input-path.md、rts-lifecycle-followup.md、settings-window-path.md、draw3-render-and-diagnostics.md。独立证据审查：evidence-review.md。

## 实际执行与结果

| 项目 | 结果 | 实用边界 |
| --- | --- | --- |
| 原片分析 | 完成79.95秒全片索引及44–53秒、末段关键帧检查 | 视频不是完整日志，录制版本hash未知 |
| 已有Debug副本正常绘制 | 成功，Down published=1与Up published=1 | 只代表该副本/该次运行 |
| 已有副本设置打开/关闭后绘制 | 成功；设置曾白屏，但WM_CLOSE后可继续绘制 | 版本标记20260811a，与HEAD源码20261014a不一致，不能据此诊断HEAD |
| 已有副本Explorer→选择→关闭Explorer→软笔 | 成功；合计3个Down/Up都发布，三条笔迹出现 | 未复现持续输入失效，关闭设置用了WM_CLOSE而非ImGui关闭按钮 |
| GUI窗口与WCT采集 | 已记录owner/focus/capture，WCT未识别到cycle | STL/驱动/COM栈未提供，单节点cycle=false不能排除所有等待 |
| 当前HEAD完整Solution Debug ARM64 | 最终exit0，448 warnings，0 errors，用时7:56 | 初次完整构建因当前worktree的Abseil实体头/库缺失而链接失败；同版完整provider后成功 |
| 当前HEAD InkeysHeadlessTests --no-window | exit0，PASS animation correctness，EraserAttribute/RecallAttribute failures=0 | 不验证真实RTS、焦点、窗口显隐或用户GPU |
| 当前HEAD GUI | 已启动隔离副本；有效复测待解锁 | WTS sessionFlags=0/sessionLocked=true，foreground=0；黑屏为锁屏环境，不计作产品故障 |
| 任务文档/探针静态检查 | UTF-8解码、JSON/JSONL、context路径、Python compile通过；task validate通过 | 无产品源码diff |

当前二进制SHA256：9316b86f5d08f0135b8e583fe490211fbcfb59f1c5c0fa055a3abea8865347fd。
已有副本SHA256：c1ae753dd96b5ac77b539854bd8fef0990d48c8695271a2cdae5c9e11dc2ddde。

环境为Windows11 ARM64、单屏2880×1920、192DPI。视频日志显示1366×768、96DPI、CloneOrMixed，并展示Inkeys64路径，不能认为两边环境等价。

## 构建环境问题的归属

首轮solution使用动态定位的ARM64 MSBuild，执行同invocation PATH规范化；当前worktree的vcpkg status记录Abseil20250512.1已安装，实际include/absl和88个absl静态库缺失，编译退到保留的旧vendor头，链接出现StatusRep::Unref等15组未解析符号（LNK2001/LNK2019/LNK1120）。这是本地provider不完整，不是本轮引入的源码错误。

随后核对主工作树完整provider的10个override版本、Abseil ABI=621ef7758b25c07a3352b68a89d7c2a32a8bd38456a7290426a5948402e0676e、20250512.1头文件与ARM64库；仅本次命令行VcpkgInstalledDir指向该只读完整安装，未混用旧头/新库，未修改工程、SDK、依赖版本或全局配置。完整solution重新构建成功。

日志：solution-head.log（首轮失败）、abseil-environment.json（环境证据）、solution-head-shared-provider.log（最终成功）、headless.stdout.log/headless.exitcode、stdout-native.log（已有副本RTS）、stdout-head.log、desktop-state.json。均位于Build/CanvasFreezeInvestigation。

## 修复方案与下一步

首选是根据一份可判别的失败现场选择最小修复分支，不同时改动所有候选。design.md给出了各分支的准入条件、修复边界、回归和回退。

1. 解锁后用已构建HEAD重测视频的完整顺序，重点包含有墨迹选择态第二次开设置、实际点击ImGui关闭按钮、关闭Explorer，再试笔和橡皮。保留失败瞬间窗口状态与日志，先不重启。
2. 若现有日志仍无法区分，用户已授权的调查范围内可加最小诊断：RTS lifecycle入口/出口与原子writer/query阶段、Host既有调用链低频外读probe、controller Down准入结果。复用Cursor有界通道，无新线程/定时器/wake、无自动恢复。
3. 若证实RTS交付停滞，修其实际生命周期/路由问题并设计安全恢复；若证实固定context解码失败，隔离失败影响域；若证实发布成功后准入拒收，修generation/交接/终态收敛。只有失败栈确认DXGI/ImGui闭环时才调整该锁边界。
4. 本轮明确不修复。行为修复后续需批准，任务保持planning；当前root_cause_status=unconfirmed，current-gui-pending-unlock。

本轮创建的隔离测试进程14204、39888、7664均已关闭，原片和用户原有配置未改。解锁后可用run-head中的同hash副本继续复测。

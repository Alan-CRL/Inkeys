# 原 Freeze Mag PrintWindow 软件内验证实施记录

## 当前实施：按用户最终收窄范围

本轮已移除独立捕获 service/runtime lease 和旧 Headless capture PoC；恢复原 Freeze 的初始化/窗口对/ready promise 与普通协调线程安排。未完成的 Window/Coordinator 后端接线已撤回，二者无最终 diff。已确认每轮6个无callback样本均63/63匹配；callback仅为可选候选，不是获取画面的依赖。新增能力仅为 harness gated 的 `Inkeys.exe --freeze-mag-printwindow-test` 与原 Host 定时读回；没有产品 Tracker/Draw3/Bar 功能、第二套 Mag 窗口或 owner RPC。

- [x] 读原定格路径和 Inkeys2 历史，并服从最新“现有设计不调整”要求。
- [x] 按归属选择性回退；删除独立 service/runtime/test 及项目和 runner 登记，保持 BOM/CRLF。
- [x] 真实 WindowService 原 callbacks/Host/Child + 原过滤/Source/Redraw + PrintWindow(2) 软件内测试；冷 hidden pair 先读，再正常 Freeze 前后 Reveal/Conceal。
- [x] 800ms 首等待、有限预热、静止重复、7 unique phases、63 分布点、真实蓝覆盖 GetPixel 正对照。
- [x] callback 真实登记/Get核对/安全直通计数/恢复旧值；本机 DWM=1 各轮6入6出，不当作通用整帧完成保证。
- [x] 最终新进程两轮 exit0（PID73216/81464）；每轮9个被接受样本63/63，首轮另有1个62/63启动样本被拒绝后重试成功。14张最终 PNG 各5,526,720像素离线逐像素核对 mismatch0。
- [x] 完整 Debug|ARM64 solution 正常宿主构建 exit0（最终19.09s、3既有warning/0error），重建后 --no-window exit0，i18n425/425 PASS，bad-argv exit2、XML/格式/diff/PE检查通过。
- [x] 顺序 trellis-check 通过，无需代码修复；核对最终构建、两轮日志、整图像素、XML与BOM/CRLF。见 research/freeze-printwindow-check.md。未commit/push。

详细结果、日志、真实线程/窗口状态、预热历史与限制：[research/freeze-printwindow.md](research/freeze-printwindow.md)。GDI11→13、USER4→5未宣称长期零增长；未做持续采样、其他平台或用户文档测试。

## 以下为已移除的独立原型历史记录

旧复选框、接口、门槛和文件列表仅解释此前实验；本轮当前实现与验证以上述记录和最新报告为准。

## 顺序
- [x] 读取父prd/design/implement及本任务规范上下文。
- [x] 独立实现 MagCaptureService、共享 Mag runtime lease、图像有效性、超时与重复启停、[ScrollFollow]基础日志，并在现有 Headless 工程中提供 no-window 单测及显式真实 PoC。
- [ ] 必须真实验证源图案变化、Host内容不可见、排除叠加窗口、静止帧与重复启动/停止，记录耗时/有效性/清理。失败在Mag方案内调查，不开始tracker/产品接入。
- [x] 使用完整InkeysRepo.sln Debug|ARM64及定向测试验证，检查首个意义错误与退出码。MSBuild同PowerShell先规范化PATH、MSBUILDDISABLENODEREUSE=1；超时至少5分钟。
- [x] trellis-check审查，记录文件、关键实现、命令/退出码、结果及遗留；审查完成不代表真实捕获门槛通过。

## Implement阶段验证结果（历史；Check结果见文末）

**真实共享运行库门槛未通过；本任务不标记完成，后续 Tracker/产品接入/持久化阶段未进入。** 冷启动真实服务与生命周期检查通过，不能替代预持 Freeze lease 用例。最终证据已对齐实际产品首次 Mag 初始化前的 STA COM 顺序；此前不等价的 fixture 与历史失败日志均保留。

| 命令 / 检查 | 退出码 | 结果 / 日志（均在 Build/ScrollFollow） |
| --- | --- | --- |
| ARM64原生 MSBuild，`InkeysRepo.sln /m:1 /nr:false /p:BuildInParallel=false /p:Configuration=Debug /p:Platform=ARM64` | 0 | `capture-build-arm64-stable.log` / `-console.log`；完整 solution 成功，最终增量0 warning/0 error |
| `InkeysHeadlessTests.exe --mag-capture-only` | 0 | `capture-only-arm64-stable.log`；缓冲/黑帧/纯色/暗纹理/静止/参数/超时与初始状态 |
| `InkeysHeadlessTests.exe --no-window` | 0 | `capture-headless-arm64-final.log`；全套含新增 capture 基础用例，末尾 PASS animation correctness；随后仅改显式PoC的STA fixture，普通套件无改动 |
| `InkeysHeadlessTests.exe --mag-capture-lifecycle-poc` | 0 | `capture-lifecycle-sta-order-arm64.log`；初始化失败回滚、回调异常、真实1秒监督超时、资源保活、Stop=0ms、忙碌拒绝、释放后真实有效像素再启；init4/uninit4 |
| `InkeysHeadlessTests.exe --mag-capture-poc-cold` | 0 | `capture-poc-cold-sta-order-arm64.log`；两轮静止、4次源变化、alpha0可绘制Host、排除、元数据、重复启停与清理通过 |
| `InkeysHeadlessTests.exe --mag-capture-poc` | 1 | `capture-poc-shared-sta-order-arm64.log`；main_com=0，130帧全部Filter/error50，0 valid；清理与原Freeze lease保留通过，真实捕获门槛失败 |
| `InkeysHeadlessTests.exe --mag-runtime-owner-poc` | 0 | `capture-poc-owner-sta-order-arm64.log`；同owner正对照和三轮交替源像素通过；private bytes增长，不能当作可上线修复 |
| `InkeysHeadlessTests.exe --mag-runtime-thread-poc` | 1 | `capture-runtime-thread-com-arm64.log`；不同owner组合均Filter/error50，无可采纳生命周期方案 |
| `git diff --check --ignore-submodules=all`、工程XML与编码/EOL检查 | 0 | 新代码UTF-8无BOM/CRLF；原有修改文件UTF-8 BOM/CRLF均保留；三份工程XML解析通过 |

所有运行使用独立新进程和可写 `TEMP/TMP=Build/ScrollFollow/TestTemp`。首次受限 runner 构建在module scan后4:59.73退出1且0源码错误；正常宿主相同solution成功，未为环境问题改生产代码或构建配置。通过vswhere定位ARM64原生MSBuild；同一PowerShell内合并为单一Path（含pwsh路径）并设MSBUILDDISABLENODEREUSE=1。无自动审批拒绝。

## 核心文件与变更

- 新增 `Inkeys/Inkeys/Graphics/MagCaptureService.h/.cpp`：独立owner/monitor、借用帧、固定Display几何、图像判定、5秒启动/1秒运行监督、非阻塞停止、实际线程退出后再启、现有异步日志。
- 新增 `MagnificationRuntime.h/.cpp`：动态能力门禁、进程级lease与诊断计数；保留首次初始化/最后使用者释放策略。
- `Inkeys/IdtMagnification.cpp`：Prepare/Shutdown窄改为共享lease；未改定格协调与窗口流程。
- 新增 `InkeysHeadlessTests/mag_capture_tests.cpp`，现有 `animation_tests.cpp` 注册基础测试与显式PoC入口；两个vcxproj和主工程filters登记源文件。
- 更新子任务design/implement和父research/capture-poc.md。父任务长期spec与任务协调由主会话维护。

## 实测与未完成项

主屏2880×1920；implement阶段历史成功cold两轮14帧(13valid/1invalid)与13帧(12valid/1invalid)，平均54.523/64.991ms，p95=67.716/79.128ms，最大70.441/79.312ms，实际10.302/8.491 results/s，启动109/156ms。共享失败约20次/秒是快速API拒绝结果，不能表述为20FPS捕获通过。

Host不可见内容依靠同屏alpha0可绘制生命周期；offscreen或SW_HIDE原始对照虽PrintWindow返回TRUE，却读到mean240/variance0的常量，未通过源phase匹配。WinSta0/Default/InputDesktop Default、session1、DWM开启、原生ARM64/WOW64=0，不能仅凭错误50归因平台全面不支持。窗口站与桌面只读取名称，没有切换或修改安全设置。

同owner三轮GDI=2、USER=4、handles=225稳定，但private bytes=310784000→376082432→440078336，尚无长期资源有界证明；没有更改生产owner策略。生命周期的阻塞回调测试真实经过监督路径，未注入实际PrintWindow永久阻塞。专用图案有独立进程、painted确认、kill-on-close Job和90秒自关闭，无遗留测试窗口。

随后已顺序执行trellis-check，ARM64最终构建及无窗口回归通过，i18n/PE/格式检查通过；x64最终结果见research/check.md。未执行真实浏览器/Word/PDF、笔/触摸、Win8.1/Win7和产品定格端到端；它们不能绕过当前共享门槛。未改依赖、版本、triplet、渲染器、配置保存；未commit/push。

## Check阶段最新结果

check只修改测试，不改生产捕获/运行库/定格源码：补完整帧元数据、直接核对阻塞回调的lease保活，并新增--mag-capture-poc-freeze-control，对齐真实产品first-owner控件创建即隐藏且global-once的前提。

ARM64完整solution exit0（8.68s、0 warning/0 error），--mag-capture-only及完整--no-window均exit0。fresh-hidden服务对照130结果/0valid、全部Filter50；cold55帧NearBlack/0valid；lifecycle监督/Stop/busy/lease保活/清理通过但最后有效像素再启失败，整体exit1。幸存原控件黑帧缺乏相同样式独立正对照，不推定捕获破坏定格。i18n425/425、XML、diff、物理编码换行和静态PE检查通过。历史成功不覆盖最新失败，阶段保持in_progress。

详细检查、构建退出与未覆盖项：[research/check.md](research/check.md)。原始实验与环境：[父research/capture-poc.md](../10-21-scroll-follow-v1/research/capture-poc.md)。

## 本轮继续实施（2026-10-21）

用户已批准完成正式取帧及全部后续阶段，上一轮仅诊断的范围限制结束。本轮验证仅无窗口或完全不在屏幕显示的HWND，禁止Computer Use/前台/可见测试；必要可见验收时停止。当前授权见[implementation-authorization.md](../10-21-scroll-follow-v1/research/implementation-authorization.md)，其优先于本文旧轮次边界。原定格Mag的无callback基础验证见research/freeze-printwindow.md（捕获子任务）；后续正式路径须分别验证。本轮不自动commit/push。

## 正式异步取帧实施（2026-10-21，本轮继续授权）

本轮已完成供后续阶段使用的正式 Capture API，生产/测试源码交给主会话顺序 check；没有把当前完整滚动批注任务标记完成。

- [x] 保留原 MagInitialize/create/readyPromise 与普通 Freeze 分工；由既有 MagnifierThread 串行执行共用控件的 Begin/Update/PrintWindow(2)/End，Coordinator 仅增 timed wait/external wake。
- [x] 新增 Graphics/FreezeCapture.h/.cpp 的 Start/Stop/GetSnapshot/借用 Frame 与可测 Session/Backend；固定 primary/source/client/workarea/display generation，top-down BGRA、session/sequence/timestamp/quality/stats，无捕获 FIFO、50ms 目标时隙、慢帧跳过。
- [x] 原 Source/client 少一像素只保留真实几何，不按宽比缩放。生产后端 MagGetWindowTransform 核对 identity，Frame 映射=1:1；不支持其他矩阵即取消。
- [x] 首次 update 等800ms、有限重试，5s内必须accepted F0；之后1s无有效结果监督撤销。黑/低信息/API拒绝不能延长期限，静止同图有效。quality/rejected/apiFailures可供后续即时Lost检测。
- [x] Stop/Freeze/超时不等待PW或已准入借用回调；迟到Read不发布；旧Read/回调/实际清理未返回保持occupied并拒绝新Start。Freeze先串行结束capture占用，再执行原事务。
- [x] 异常Host销毁只关门；inFlight时保留DIB/runtime、跳过MagUninitialize并标记本进程不可再启，不阻塞Overlay owner。正常仍stop -> MagnifierThread join -> WindowService StopAndJoin，由原owner uninit。此窄接口未拦截OS已执行的HWND销毁，不能宣称所有异常系统行为已实测。
- [x] 两次完整InkeysRepo.sln Debug|ARM64正常宿主构建均exit0（36.00s/20.72s，3个既有hashlib++ C4267、0error），原生ARM64 MSBuild/vswhere/单一Path/禁node reuse/仓库TEMP；未改SDK、依赖、PDB/工具链。
- [x] `--freeze-capture-only`最终exit0；完整`--no-window`exit0、末尾PASS animation correctness。新增测试无HWND、使用fake backend像素；真实steady_clock监督线程覆盖阻塞消费者1s撤销。
- [x] 工程XML解析、源文件diff检查、原BOM/CRLF核对通过（Coordinator原无BOM）；新增代码关键中文注释。
- [ ] 正式MagnifierThread上的真实屏幕像素/排除/持续采样/资源增长及普通Freeze前后可见验收。本轮禁止可见测试，因此没有运行旧`--freeze-mag-printwindow-test`，没有启动普通主程序/Computer Use。旧owner诊断仅为基础证据，不能代替正式跨线程路径。

本阶段文件：Inkeys/IdtMagnification.cpp；Inkeys/Inkeys/Graphics/FreezeCapture.h/.cpp；Inkeys/Inkeys/UI/Freeze/Freeze.MagnifierCoordinator.h；主vcxproj/filters；Headless vcxproj/animation runner/freeze_capture_tests.cpp。未接Tracker/Draw3/Bar，未commit/push。

本轮接口、线程/时间/质量合同、全部日志、测试与未覆盖边界详见[research/production-capture.md](research/production-capture.md)。原始结果位于Build/ScrollFollow/production-capture-build-arm64[-final].log、production-capture-only-final.log、production-capture-headless.log。后续顺序check如改源码，应另外保留其修订和验证，不覆盖本轮实施证据。

## 正式取帧顺序 Check（本轮）

- [x] trellis-check修复窗口ready跨线程atomic与stop通知丢失竞态，保留原Mag/创建线程结构；新增idle/active各64次真实监督stop/join回归。
- [x] 最终完整ARM64 solution exit0，29.32s，62个既有warning/0error；定向exit0约2.48s，非法参数exit2且输出0B；完整--no-window exit0约109.92s，stderr空。
- [x] 三份工程XML、十份代码/工程UTF8/原BOM/CRLF及源码diff检查通过，最终验证后源码哈希不变。
- [ ] 正式MagnifierThread实屏像素、持续采样/排除和系统销毁边界仍未验收；上轮owner诊断不能代替，本轮禁止可见操作。允许进入纯离线Tracker，不将父功能标记完成。

完整审查记录：[research/production-capture-check.md](research/production-capture-check.md)。本轮未commit/push。

# 独立 Magnification 连续捕获与 PoC实施记录

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

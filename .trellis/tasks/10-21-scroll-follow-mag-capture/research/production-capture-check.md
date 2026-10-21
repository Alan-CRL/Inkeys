# 原 Freeze Mag 正式异步取帧检查（2026-10-21）

按当前 check.jsonl、prd/design/implement、父任务 implementation-authorization.md 和相关 native-desktop 规范检查。结论：**本阶段源码及无窗口检查通过，可进入纯离线 VisualMotionTracker 实现。正式 MagnifierThread 跨线程读回的实屏像素、排除、持续帧率和产品交互仍未验收，不能把本报告作为完整滚动批注已通过的依据。**

本检查只修改下列四个生产/测试文件并新增本报告。未改主会话维护的任务/spec、实现者 production-capture.md 或 Window 框架；没有派发子代理、运行普通 Inkeys.exe、创建可见窗口、执行旧 --freeze-mag-printwindow-test、Computer Use、commit 或 push。

## Findings (fixed)

- File: `Inkeys/IdtMagnification.cpp:30`、`Inkeys/IdtMagnification.h:12`
  - Issue: 原 owner 在创建/退出时写 `magnificationCreateReady`，新增公共 Start 和持续 backend 检查会从其他线程读该 plain bool；异常 owner 退出等交错下缺乏 C++ 同步。
  - Fix: 定义及 extern 声明统一改为 `std::atomic<bool>`，添加直接 `<atomic>` 依赖和简短中文所有权注释；保留现有初始化/窗口/普通定格线程安排及全部调用点语义。完整 solution 编译确认依赖方兼容，没有扩展为句柄或 Window 框架重构。
- File: `Inkeys/Inkeys/Graphics/FreezeCapture.cpp:325`、`InkeysHeadlessTests/freeze_capture_tests.cpp:323`
  - Issue: RunSupervisor 的 stop_callback 原来只调用 notify_all，没有与 condition_variable 等待的谓词锁同步。停止可能落在检查 stop_requested 与真正 wait 之间；idle 无限等待下会丢失通知，退出 join 可能挂住。
  - Fix: stop_callback 取得同一短状态锁再通知；callback 仍在 unique_lock 之前构造，退出时先释放锁再注销 callback。新增 idle/active 各 64 次实际 jthread request_stop/join 回归，交替 yield 改变时序；测试不创建 HWND。既有真实一秒监督/阻塞消费者用例也继续通过。

## Findings (not fixed)

未发现本阶段范围内、需要继续修复且尚未修复的已确认源码问题。以下是明确保留的设计或验收边界：

- 新正式路径的 Begin/Update/Read/End 位于既有 MagnifierThread；旧 3ace726b 的 PrintWindow 诊断在原 HWND owner 上执行。旧轮真实像素证据不能替代当前跨线程路径的受控像素、变化内容、完整角色排除、20FPS/长期资源和普通 Freeze 前后验收。本轮仅执行假 backend 的无窗口状态测试，未声称真实屏幕采样通过。遵守本轮不可见测试限制，没有运行可见旧入口。
- 异常销毁采用现有的关发布门、inFlight 禁止 MagUninitialize、abandoned 禁再次初始化及旧 DIB/消费者自然排空策略。当前 WindowGroup 实际先销毁 MagnifierChild，再销毁 Host；Host WM_DESTROY/Shutdown 回调不等于拦截了 Windows 已经执行的 child 销毁。实现报告已披露这一边界。没有修改 Window 生命周期模块，也没有在本轮对真实阻塞 PW 与 HWND 销毁做现场证明；后续产品验收不能把纯 Session 测试当作此类系统调用保证。
- 同步 PrintWindow 永久不返回时，Stop 会撤销发布，但实际线程 join 不具备本阶段新增的有界保证。保持资源而不是提前释放/强杀线程符合当前设计；正常进程退出仍依赖既有 producer 先退出、窗口后销毁顺序。该边界不阻止独立的纯离线 Tracker 工作。

## 核对结果

- 原 PrepareMagnifierWindow / HostCreated / ChildCreated、Overlay STA、readyPromise 及普通 Freeze 事务保留；没有第二对 Mag、独立 runtime lease 或 owner RPC。新增监督线程不操作 HWND/Mag/DIB。
- Freeze 发布先撤销 capture 发布；MagnifierThread 在同一串行者上等待当前 Read/已准入回调自然返回并 Drain，然后执行原 Freeze。Session occupied/inFlight 阻止旧调用和清理未结束时重启；Pump 提交前再次核期限和发布门，迟到结果不能恢复旧发布。
- Start 只接受请求；初始五秒期限以第一个 accepted F0 为界，稳态一秒只由有效 accepted 推进。黑/低信息/API 失败不刷新期限，Snapshot 立即暴露质量及失败统计。静止同图仍有效，慢帧跳过绝对时隙，无图像 FIFO。
- 固定 Display snapshot 的 primary/source/workArea/generation；backend 前后核当前 HWND、系统尺寸、真实 client/source 和 identity Mag transform。source 少一像素按原几何保留，比例明确为 1:1，不按矩形宽比或 DPI 重缩放。
- Frame 是 alpha=255 的 top-down BGRA 借用；Stop 不等待已经准入的回调，回调对象必须保活到 occupied=false。实际 Read/回调返回后才清理 DIB/闭包，消费者异常撤销发布。API/质量状态不能代替真实像素正确性。
- 主程序、Headless 工程、filters 和 runner 均登记新文件；--freeze-capture-only 严格单参，默认 --no-window 也执行新增状态用例。未改变工具链、SDK、依赖、存储或配置。
- 当前 spec 已由主会话更新正式串行取帧、监督和异常保留边界；本检查不覆盖其写入。建议后续阶段继续明确区分状态/合成图证据与当前生产线程的实屏验收。

## Verification

- Lint: **pass**。源码 git diff --check --ignore-submodules=all 通过；新文件另查尾随空格和 EOF；三份工程 XML 解析通过；10 份生产/测试/工程文件严格 UTF-8、CRLF 和 BOM 检查通过。Coordinator 保持原无 BOM，其余实现/测试文件按接手时的实际 BOM 保留。没有发现独立 lint/clang-tidy 目标。静态记录为 `Build/ScrollFollow/production-capture-check-static.json`。Git 子模块目录在受限 runner 中不可访问，因此检查明确仅覆盖主树，不宣称验证了子模块。
- TypeCheck: **pass**。通过 vswhere 定位 ARM64 原生 MSBuild，require_escalated 正常宿主完整构建 `InkeysRepo.sln /nologo /nr:false /m:1 /p:BuildInParallel=false /p:Configuration=Debug /p:Platform=ARM64 /p:PreferredToolArchitecture=arm64 /v:minimal`；同一 PowerShell 规范单一 Path，设置 MSBUILDDISABLENODEREUSE=1、TEMP/TMP=Build/ScrollFollow/TestTemp，进程等待上限 10 分钟。退出 0，MSBuild 29.32 秒（外部 29,641ms），62 个现有 Setting/IdtMain/hashlib++ 转换 warning，0 error；新增修改没有编译 warning。日志 `production-capture-check-build-arm64.log`、`-console.log`、`-stderr.log`。
- Tests: **pass**。重建产物 `InkeysHeadlessTests.exe --freeze-capture-only` 退出 0，2,479ms，包含新停止唤醒回归；`--freeze-capture-only --unexpected` 退出 2、输出 0B；完整 `--no-window` 退出 0，109,923ms，包含新增 capture PASS 且末尾 `PASS animation correctness`，stderr 为空。日志 `production-capture-check-only.log`、`production-capture-check-bad-argv.log`、`production-capture-check-headless.log`、`production-capture-check-headless-stderr.log`，均在 Build/ScrollFollow。
- 当前重建产物 SHA-256：Inkeys.exe 为 `9E03E4F1613729D03EF736EF5640DD34C005AFC38DA3889463ADBAA1B5A19381`；InkeysHeadlessTests.exe 为 `FCDA2A652F6836F457AD19DA1C57F56BFC6EA5E3F0DFE463232A24D5628ACC46`。未运行主程序，也未把文件哈希当作实际 GUI 验收。

没有更换构建系统或为环境问题改源码，没有遇到本次自动审批拒绝。未执行真实屏幕/前台/置顶、其他架构、Win7/8.1、实际笔/触摸、浏览器/Word/PDF/视频、长期性能或完整产品启动/退出。后续必要屏幕验收依然是父任务的停止门槛。

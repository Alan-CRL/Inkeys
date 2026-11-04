# R2 五窗 Touch 修复验证

日期：2026-11-03；分支：bugfix/color；共享样式提交 cbd2294d 之后的未提交输入修复。最终源的Debug与全部自动回归、Release x64构建均通过；设备验收待用户完成。

## 修改与证据

- 主栏及四分页各自持有 Context，共用固定五槽登记与规则；Touch 原句柄由原 WndProc 读取/关闭一次，按值转交到实际屏幕命中的合格目标。
- Down 前核真实候选及已呈现区域，Move/Up 锁定 Down owner；Page 统一拒绝所有 Pointer 签名兼容 WM_MOUSE，消除现场 72d Down/7ad Up 分拆会话的问题。真实 Mouse/触摸板/原 Raw 路径保留。
- 完整 registration/local generation 身份用于取消，含零捕获票据、队列溢出和重入；捕获变更只取消目标，源生命周期失效可取消来源关联路由。重入后复核来源/目标，旧代不清新代。
- 主栏发布完整不可变已呈现命中快照；PPT 原 Down 立即与可选长按重复、白板有效 Up 单次、主栏连续属性和可选触觉保留。未增加分页震动、未改 Hook/Draw3 输入或驱动配置。
- 根因与原始日志行号见 research/tablet-hover-root-cause.md；规划/独立审查/生产测试边界分别见 touch-fix-design.md 与 research/touch-fix-check.md、bar-touch-probe-implementation.md、touch-route-tests.md。

## 已完成静态检查

- git diff --ignore-submodules --check 通过；所有修改源保持原 UTF-8 BOM 身份及 CRLF，无裸 LF。新 header 为 UTF-8 无 BOM/CRLF。
- task.py validate 当前任务通过，implement/check manifest 各 7 条有效引用。
- 未修改构建系统/依赖版本。既有两个 CSO 构建产物与其他任务内容保留，未回退/清理。

## 构建与运行记录

统一使用既有 InkeysRepo.sln，vswhere 动态定位 ARM64 原生 MSBuild；同一 PowerShell 中规范化单一 Path 并禁用节点复用。Debug 构建上限 900 秒，headless 上限 900 秒，Release x64 构建上限 1800 秒。进程隐藏启动并重定向，不启动产品交互 GUI。

- Debug|ARM64 最终源构建：exit 0，41.04 秒。第一次编译发现 Bar.Button.cpp 缺少直接包含 Bar.BottomDock.h，已一行补齐并通过后续构建；未改变项目配置绕过。
- --ui3-input-worker-test：exit 0，0.80 秒；stderr 明确 [UI3TouchProbe] PASS（真实 Prepare/Publish/Probe）及 [UI3Input] PASS（真实 mailbox/dispatcher/continuation/deadline/cancel）。
- --state-mode-production-test：exit 0，0.10 秒。
- --pen-width-production-test：exit 0，0.14 秒。
- --draw3-pen-width-test：exit 0，0.08 秒。
- --draw3-control-fence-test：exit 0，0.47 秒。
- InkeysHeadlessTests.exe --no-window：最终版本 exit 0，306.60 秒，无超时，最终输出 PASS animation correctness；真实 RunUiPointerTests/RunTouchRoutingTests 已由主入口累计失败数。补充源失效取消检查后完整重跑通过。
- Release|x64：exit 0，115.29 秒，无超时。PE Machine=0x8664，39,866,880 字节；ui_schema、event=ui_input v=1、route_down 与新增 fallback guard 诊断标识均存在。

原始 stdout/stderr、MSBuild 日志与 exit/timedOut/seconds JSON 保存在 ignored TestResults/touch-fix/ 及 TestResults/touch-fix-x64/。

## 设备验收边界

未运行 GUI、Computer Use、M6 或另一块数位板测试。无窗口测试证明生产路由/生命周期、快照/几何及业务策略回归，不证明驱动实际发送/窗口命中结果；也不证明原生 ULW alpha 边界。按 touch-fix-capture.md 由用户完成一轮持续感应跨五窗、长按与连续属性复核并交回完整日志。

T1-T7 的实际设备行为仍待确认；T8 的Debug构建、软件回归与Release x64产物均通过。任务保持 in_progress，不归档。本轮不 commit/push。

## 最终测试产物

- Build/x64/Release/Inkeys.exe，最后写入时间 2026-11-03 00:57:52（本机时间）。
- SHA256：F1FAE891BB70EDB8813C0AB10E5EC1FF13B1AA9A609EA4CF04B93B4B27AF6E04。
- 二进制静态核对记录 TestResults/touch-fix-x64/artifact.json。Release x64只构建与静态核验，没有在ARM64主机启动该GUI。
- 本轮出现的Setting/第三方数值转换等既有warning未扩成无关修复；最终Debug/Release均无error。
- 实机复核命令与连续操作步骤见 [touch-fix-capture.md](touch-fix-capture.md)。本轮不提交commit，不push，不归档待验收任务。

## 追加审查：覆盖缺口仍存在（2026-11-03）

后续用户提供另一份旧版M6-input.txt（SHA256 696F0F2560F59FBA2502E7065972478F70C7108883FB6CFBEA16FDE4DEE623BB），全文件未见Touch，仅Pointer+Pen兼容Mouse；当前没有Pointer业务入口，还会过滤旧版成功的分页Pen Mouse。上述构建与回归PASS仍真实，但不能证明该新增场景已覆盖。详细证据见research/additional-log-coverage.md；本轮不改代码、不重新构建、不要求无环境的用户立即复测。

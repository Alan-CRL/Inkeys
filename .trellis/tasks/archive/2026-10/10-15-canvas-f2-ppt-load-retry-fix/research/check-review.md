# F2 独立检查记录

日期：2026-10-15。检查角色：`trellis-check`（`/root/check_f2_f3`）。本记录是源码/合同检查，构建和运行由主会话串行执行；不得将下面的静态结论转记为测试已通过。

## Context and Scope

已读取本子任务及 F3 的 PRD、design、implement、check.jsonl，父任务设计/执行计划、专项 F2/F3 研究和调查记录。按大型规范加载合同完整分块读取了原始 `input-and-ink.md`（674 行）与 `draw3-integration.md`（440 行），并核对 C++、资源/错误、构建和橡皮规范。注入上下文的保存文件也已读取。

检查文件为产品 `Draw3.PresentationAutoSave.cpp`、`Draw3.DrawingController.cpp`，以及 F3 的 coordinator 和 headless 测试。共享文件在 F1 迁移期间仅只读；主会话开始构建后所有编译源保持冻结。本检查者没有修改产品源码、项目配置或测试源码。

## Findings (fixed)

无本检查者自行修正的 F2 问题。

## Findings (not fixed)

- **相邻 F1 集成登记缺口，已报告主会话协调**：`inkStrokeModelerTestTests/inkStrokeModelerTestTests.vcxproj:121,126` 直接编译产品 `Draw3.AutoSave.cpp` 和 `Draw3.PresentationAutoSave.cpp`。两文件迁移后引用 `DiagnosticOutput::Printf/Text`，但检查当时该工程未登记 `Draw3.DiagnosticOutput.cpp`，只有主工程有登记。该独立测试不在 `InkeysRepo.sln` 内，主 Solution 成功不能证明它可链接。建议由 F1 所有者补齐该消费者的最小源码项并验证既有独立测试入口；本检查者按共享编辑/构建冻结约定未改工程。
- 未发现需要修改的 F2 生产逻辑问题。类型检查和实际测试结果仍待主会话，不以静态审阅替代。

## Production Path Review

- `LoadPresentation` 的新增分支只将真实 reader `IoError` 保持为 persistence `IoError`；它位于原有 Complete/provenance/sourceRevision 严格 guard 之前。索引存在但文件不可读不会转成 `NotFound`；部分恢复、坏头/尾、超限、revision 改变、导入或 workspace 身份不符仍走原拒绝路径。
- 索引/target/source/key/binding/session/track 选择仍先于读取，未被本补丁绕开。`SelectStorageTrack` 只检查索引和轨道身份，不提前读取 fixture 的 UInk 内容，因此真实独占文件测试确实到达本次 reader 分类点，而非由另一个文件检查提前返回同名错误。
- Controller 的 completion 仍先经 `RoutePresentationCompletion`、slot generation/lane 及 loadPending 门。Current 失败保持 `persistenceInitialized=false`，仅 IoError 调 `OnIoError`；成功必须通过 lane 验证、真实 materialize 和 `InstallLoadedActivePresentationSlot`，才解除未加载准入。PreviousInterval 失败路径未新增 current-load 自动重试。
- 原 250/500/1000/2000 ms 四次预算、pending 防重复、target/generation 匹配、mutation/queued 安全点和 PrepareExit 单调门均未改变。永久 I/O 失败仍可能耗尽预算；这是保留的已批准边界。

## Regression Fidelity and Limits

新增 `RunPresentationWorkerLoadRetryProbe` 由既有 `--draw3-ppt-current-load-retry-test` 实际调用。它使用真实 worker、唯一临时根、真实非空 UInk/index 和不共享文件句柄；锁保持到真实 completion，按原 retry helper 安排第二次真实读取，再走生产 materialize/install 核原墨迹和准入。source/key、workspace、合法源 revision 变化、坏 header 和截断 tail 均由真实读取产生负例，并检查文件/index 字节未被空文稿覆盖。原 probe 继续覆盖四次耗尽、换 target/generation 和退出屏障。

新 fixture 不是完整 Host/Controller::Run 或 Office GUI。其 source/key 冲突也不能改称实际 foreign session 测试。既有 `inkStrokeModelerTestTests --presentation-autosave-only` 包含外来会话/独立 track 场景：原策略可能为外来 Base/sidecar 建立独立 session track 并返回该新轨的 `NotFound`，同时保留旧文件；不能宣称所有 foreign session 一律 terminal 拒绝。应按真实入口分别报告运行范围。

## Spec Sync

建议主会话在实测后把“reader IoError 保持分类并复用四次退避；不可读的已索引 UInk 不是 verified empty；严格安装后才解除准入”写入 Draw3 持久化合同。保持既有外来会话独立 track 选择和本轮故障归因限制。

## Verification

- Lint/static format：`git diff --check` 对五个 F2/F3 检查文件返回 0。
- Encoding/EOL：五文件均可严格按 UTF-8 解码；四个 Draw3 文件保持 UTF-8 BOM，headless 测试保持无 BOM；工作树均为 CRLF、无 bare LF。`git ls-files --eol` 为 `i/lf w/crlf attr/text=auto`，`core.autocrlf=true`；diff 未出现整文件换行改写。
- TypeCheck：待主会话完整 `InkeysRepo.sln Debug|ARM64` 编译结果；本检查者未运行编译，避免并发 MSBuild 缓存或构建中写源。
- Tests：本检查者未运行。主会话应执行 current-load-retry、pending-topology-load、headless，并按相邻链接修正结果运行独立存储回归；实际 Office、物理触摸/笔、Win7 和多屏不得由这些测试外推。

## Reviewed Source Identity

本次审阅结束时工作树文件 SHA-256：

- `Draw3.PresentationAutoSave.cpp`：`1e63824aed25a893651ff5adb43b9d58ee34032b98b6d2a7f255420b3a5c3577`
- `Draw3.DrawingController.cpp`：`5d604bbe4cd8ba7213b37eabe84e2c66190c081fc5a38f4ee141fea7dc0486e3`

后续若修改上述文件，应按新 diff 补审并重新运行受影响验证。未创建 commit/push。

# F-044 PPT UInk/index 跨文件提交：实施与证据

后续安全复审已确认本报告中的 `RetireUnreferencedVersions` 按路径删除存在竞态；**最终工作区已移除自动旧版 GC**，并以大小写折叠索引唯一键拒绝 Windows 路径别名。下面关于“两代文件/回收”的段落仅记录初版实现与当时的 Stage3 测试，不代表最终行为。最终合同、红6→绿0 证据及无界磁盘占用风险见 [ppt-atomic-gc-hardening.md](ppt-atomic-gc-hardening.md)。

日期：2026-09-28。状态：实现完成；独立 Draw3 Debug|ARM64 编译与隔离测试已验证；主 Solution Debug/Release、独立 diff review、真机 Win7/Office/断电仍待验证。未提交 Git。

## 改动与合同

- `Draw3.PresentationAutoSave.cpp`：`ReadIndex/DecodeEntry` 同时接受严格 schema v1 固定 `files/<G>.uink` 和 schema v2 的 `files/<G>_<txnGuid>.uink`；v2 也可保留未改动的 v1 entry。路径精确绑定 `fileGuid`、规范小写事务 GUID，不接受穿越、反斜杠、错误 GUID 或在 v1 中引用 v2 路径。新 `CommitIndex` 写 schema v2。
- `SaveVersionedUInk` 对每次提交生成唯一物理版本，用既有 `CreateNewLogicalFileWithIdentity` 保留 UInk header 的逻辑 `fileGuid` 并完成写入、flush、严格自读；仅随后 `CommitIndex` 原子切换主索引。旧物理文件保持不变，备索引继续指向它。索引失败时新文件仅为孤儿/进程内 pending，fresh Service 从旧主索引严格读旧版本。首次 index 失败且无已有 entry 时，同根同会话重试仍严格校验 pending 后合并，避免丢 Clear/tail canonical。
- `RetireUnreferencedVersions` 仅在索引发布成功后检查旧备索引记录的版本；重新解析主/备索引，排除所有主/备/pending 引用，拒绝 reparse 文件/目录，只在 UInk 完整、header GUID 和完整 source revision 一致时删除。v1 固定文件、未知孤儿与失败清理均保留。连续无故障保存只保留主+备两版；故障孤儿可能留下，需要单独所有权证明才可将来清理。
- `Draw3.PresentationAutoSave.cppm` 增默认空的双 event 测试断点；仅隔离测试显式配置，普通产品没有触发命令。真实 worker 在 UInk 成功后、index 发布前 signal/wait，最多等待 30 秒。`contact_input_tests.cpp` 增测试程序显式 `--presentation-atomic-child` 分发；未改产品 EXE 命令行。
- `presentation_autosave_tests.cpp` 增上述事务/路径/回收/子进程用例，并按两代物理文件调整原文件数量断言；保留 foreign session、root change、Boundary FIFO、Tail latest-wins 与已有 UInk round-trip 测试。

本改动没有改变 page-index→StableSlideId 的业务绑定选择。用户已选择旧 page-index 文件保留、新 Stable 会话独立保存；F-041 sidecar 是另一改动单元，当前旧 `bindingUpgrade` 路径仍需 F-041 实施与验收。F-041 设计原文的 sidecar schema v1/固定文件路径须与本次统一 v2 writer 合同同步。

## 红→绿原始证据

1. 真实 worker 故障断点红测：`TestInterruptedSecondCommitKeepsPreviousVersion` 在第二次 UInk 成功、index 故意失败时，旧文件原字节断言失败，fresh Service 严格加载 `SourceChanged`；`TestSecondCommitBackupStillMatchesPreviousUInk` 在第二次 index 已提交、主索引损坏时，旧备索引所指文件原字节断言失败，严格加载同为 `SourceChanged`。旧实现完整测试 exit 1，共 4 条预期新断言失败；`TestResults/release-hardening/ppt-atomic-old-version-red-build-debug-arm64.log` 构建 exit 0；`ppt-atomic-old-version-red-full.stderr.log` 保留具体表达式和状态。
2. v2 初版：`inkStrokeModelerTest.sln Debug|ARM64` Build exit 0；沙箱外仅隔离项目测试数据的完整 `inkStrokeModelerTestTests.exe` exit 0。原始日志 `f044-stage2-build-debug-arm64.log`、`f044-stage2-standalone-debug-arm64.{stdout,stderr}.log`。
3. 扩展版：同一 Solution Debug|ARM64 Build exit 0；完整测试进程 exit 0，日志 `f044-stage3-build-debug-arm64.log`、`f044-stage3-standalone-debug-arm64.{stdout,stderr}.log`。新增测试分别覆盖 v1 fixture 严格读取→v2 切换且 v1 backup/文件字节保留；五种无效路径/schema 拒绝及恢复有效索引；三次提交后主+备两版、未知同形文件原字节仍在；真实子进程在 UInk/index 断点被信号唤醒后以本次 `PROCESS_INFORMATION.hProcess` 强杀，等待旧 PID 消失、核退出码 `0xE044`，fresh Service 严格读取旧版本。以上断言被 `RunPresentationAutoSaveTests` 调用并参与完整进程退出码；本轮没有每个子用例的独立运行日志。
4. `git diff --check` exit 0；四个修改文件均保留 UTF-8 BOM 和 CRLF。编译时仍可见现有 `Draw3.SpeedEraser.h` C4819 与第三方 modeler C4305 警告，本单元未改其源。

## 局限与待复审

- 进程级强杀只证明命中受控断点时旧索引/UInk 组合可由同 session override 的 fresh Service 严格读取；不是 UEF 崩溃捕获、断电耐久性、新进程可见画布恢复、Office/WPS 联动或 Win7 真机验收。
- 没有在 Win7 SP1+仅 KB2670838、Win32/x64/ARM64 Release、真实 Office/WPS、磁盘满/拒绝写入/断电与文件系统 cache reorder 上运行此版本。F-044 未触碰 Draw3 图形路径；用户实测的 Win7 `FLIP_SEQUENTIAL` 以及只选 DComp/ULW 的约束保持。
- schema v2 新提交不保证旧二进制能降级读取；新 reader 已在测试中读取 v1。独立 reviewer 仍需检查 v1/v2 路径解析、index ReplaceFile 失败边界、pending 保存语义、保守 GC 对 reparse/未知文件的处理，以及清理的异步 I/O 开销。
- GC 当前以路径作属性/内容验证后再 `DeleteFileW`，named mutex 只能约束本服务 writer；若威胁模型包括另一个有相同目录写权限的进程在两次调用之间更换文件或祖先 junction，仍有 TOCTOU 残余。此场景尚无故障注入或安全证明，独立 reviewer 应决定改为句柄绑定删除或保守停用自动 GC，不能把已做路径校验称作完整竞态防护。
- 15 秒强制退出可放弃未 durable 的 pending 保存，但本单元保护强杀前已 durable commit 的旧索引/UInk。完整 shutdown/restart 链和默认数据恢复能力由父任务单独验收。

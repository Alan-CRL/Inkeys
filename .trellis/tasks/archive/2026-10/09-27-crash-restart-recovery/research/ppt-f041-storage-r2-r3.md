# F041 Storage R3/R2 分批修补

日期：2026-09-28。独立复审来源：`ppt-f041-storage-independent-review.md`。实施只触及 `Draw3.PresentationAutoSave.cpp` 与真实生产 `PresentationAutoSaveService` 的 standalone 测试 `presentation_autosave_tests.cpp`；两项分别取红、修补、绿验，不修改 Controller、三轨 selector、F044 版本事务或 F048 known SlideID。

## R3｜跨代 pending 复用同一 GUID｜条件性 Storage API P2

- 触发：第一代 `slotGeneration=11` 保存新文稿，在 UInk durable 写成后注入 `failIndexCommit`，Service 保留未发布 pending。第二代 `slotGeneration=12` 在同 root/source/track 使用相同 file/workspace GUID 提交保存。
- 根因：`ConflictsWithOtherLogicalFile` 的 pending `sameLogicalFile` 只比较 track、mode、source、file/workspace GUID，漏比较已在 `PendingKey` 中的 generation；不同槽代次因此被当成同一逻辑文件。
- 红测：`TestPendingGenerationCannotReuseIdentity` 经真实 Service 依次执行失败保存、跨代保存、旧代 pending 读取。standalone Debug|ARM64 Build exit 0，完整 Tests exit 1，两项预期失败：第二代没有得到 SourceChanged，且主 index/版本数变化。原始日志：`TestResults/release-hardening/f041-pending-generation-red-build-debug-arm64.log`、`f041-pending-generation-red-debug-arm64.{stdout,stderr}.log`。
- 最小修复：`sameLogicalFile` 增加 `std::get<5>(key) == request.slotGeneration`。同代 retry 行为不变；跨代同 GUID 在创建第二份版本前拒绝。
- 绿测：相同 standalone Debug|ARM64 Build exit 0、完整 Tests exit 0；红 2 项转绿。测试确认第二代 SourceChanged、主 index 未发布、旧 UInk 字节与文件数保持、旧代 pending 仍可严格 Load。原始日志：`TestResults/release-hardening/f041-pending-generation-green-build-debug-arm64.log`、`f041-pending-generation-green-debug-arm64.{stdout,stderr}.log`。
- 可达性：当前 Controller 新槽通常另建 GUID，不能把此构造 API 缺口等同已复现的用户串页；它修复了 Storage 本身的跨代身份边界。Controller 联合运行和 Office 真会话未由本单元证明。

## R2｜大写 file GUID 的 v1 index 可读不可续存｜条件性旧数据兼容 P2

- 有效触发：单条 schema v1 index 的 `fileGuid` 与固定旧物理路径使用合法大写 GUID，workspace GUID 保持规范文本；旧 UInk header、source revision、session/key 与索引严格一致。真实 Service 先 Load 到旧笔迹，再以相同 GUID、同代次保存改变后的内容。
- 根因：Load 通过 `ParseUInkGuid` 将旧 file GUID 当作值校验，但 Save 原先用大写 `found->fileGuid` 与规范小写 `FormatUInkGuid(snapshot.fileGuid)` 做文本比较，提前返回 SourceChanged。直接用旧大写文本构造 v2 版本路径也不符合 v2 canonical 路径规则。
- 首轮排除：首次测试 fixture 还将 `workspaceGuid` 大写，导致旧 Load 在当前生产 `LoadPresentation` 的 workspace 文本比较处返回 Invalid；该轮有两个 FAIL，不能作为“可 Load 不可续存”的 R2 红证。已只修测试 fixture，不修改生产来掩盖无效前提。日志：`TestResults/release-hardening/f041-uppercase-v1-red-debug-arm64.stderr.log`。
- 可信红测：修正后的 v1 旧 Load 通过，只有 Save 预期 Committed/Base/gen21/GUID 断言失败，实际返回 SourceChanged；standalone Debug|ARM64 Build exit 0、完整 Tests exit 1。日志：`TestResults/release-hardening/f041-uppercase-v1-red-fixed-build-debug-arm64.log`、`f041-uppercase-v1-red-fixed-debug-arm64.{stdout,stderr}.log`。
- 最小修复：对已解析的旧 `fileGuid` 和请求 GUID 做值比较，继续按旧 index `relativePath` 读取并严格校验；写入新 v2 物理版本前使用请求 GUID 的规范小写文本，写成后将新 index `fileGuid` 同步规范化。旧文件不会原位覆写；原 index 通过备份事务保存。
- 绿测：相同 standalone Debug|ARM64 Build exit 0、沙箱外完整 Tests exit 0，红 1 项转绿。测试实际断言 v2 index 的 file/workspace GUID 规范文本、新物理版本路径、旧 v1 UInk 原字节、backup 与旧 v1 index 原字节一致，以及 fresh Service 严格 Load 到已清空的新内容。原始日志：`TestResults/release-hardening/f041-uppercase-v1-green-build-debug-arm64.log`、`f041-uppercase-v1-green-debug-arm64.{stdout,stderr}.log`。
- 额外发现：v1 `workspaceGuid` 虽通过索引语法验证，若大写则当前 Load 用规范文本与原文比较而返回 Invalid。它不属于本次“旧 Load 已通过再续存”的 R2 可信红绿；需另列旧数据兼容条件问题，不能把此首轮失败算作 R2 修复证据。
- 容量：迁移创建一份新 UInk，保留旧版及索引备份；这是 F044 最后有效版本保护的预期成本。本单元未引入 GC，也未声称测得磁盘或性能改善。大量续存时的长期版本增长仍是父任务已知 P2 容量风险。

## 边界与交接

- 两个修改文件维持 UTF-8 BOM 和 CRLF，`git diff --check` 无报错；未提交、未打开 GUI。
- standalone 测试只证明本机 Debug ARM64 的生产 Storage 代码路径；主 Solution Debug/Release、Win7 SP1 + KB2670838、Office 真会话和断电恢复由父任务分别验证。
- R2 旧版大写 workspace GUID 的 Load 条件缺口仍在；本测试验证的是大写 file GUID 与旧路径的兼容续存，不代表所有大小写组合都已兼容。

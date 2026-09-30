# F041 Storage：旧 v1 大写 GUID 的同 Service pending 回读

日期：2026-09-29。来源：`ppt-f041-uppercase-workspace-r4.md` 与 Storage 独立审查确认的条件性 P2。范围为 `Draw3.PresentationAutoSave.cpp` 的同代 pending 身份比较、真实 Service 回归 `presentation_autosave_tests.cpp`；另按最终 HF reviewer 要求独立清理不再被生产使用的旧绑定迁移 helper。

## R4-Pending｜条件性恢复错误：已确认失败→standalone 已验证通过

- 触发：合法 schema v1 索引的 file GUID 或 workspace GUID 使用大写文本。主索引及旧物理 UInk 已提交，为旧版 A（一笔）；同一个 `PresentationAutoSaveService`、同一个 slotGeneration 提交清空后的新版 B，注入 `failIndexCommit`，使新 UInk durable 写成、v1 主索引保留且 pending entry 使用规范小写文本。随后同 Service `Load` 应严格返回 B，新建 Service 因没有内存 pending 应严格返回上次已提交的 A。
- 根因：`LoadPresentation` 在 track/source/generation 已匹配后，仍以原文比较 pending 与主索引的 `fileGuid/workspaceGuid`。两者 GUID 值相同但大小写不同就跳过 pending，错误读取旧版 A。这不说明主索引损坏，也不授权将 B 冒称已提交。
- 红测：既有 v1 夹具扩展两条隔离变体（大写 file GUID、大写 workspace GUID），分别核旧 v1 Load 一笔、失败 Save 为 IoError、主索引与旧 UInk 原字节不变、未发布版本文件数为 2、fresh Service 仍读 A。唯一两项失败为**同 Service pending Load 应读空的 B**。standalone `inkStrokeModelerTest.sln Debug|ARM64` Build exit 0、沙箱外完整 Tests exit 1/2 failures。原始日志：`TestResults/release-hardening/f041-r4-pending-red-build-debug-arm64.log`、`f041-r4-pending-red-debug-arm64.log` 第 48/50 行。
- 最小修复：`LoadPresentation` 在选择同 track/source/generation 的 pending 后，将 pending/主索引的 file/workspace GUID 各自解析，按 GUID 值比较；解析失败继续 fail closed。未变更 session、presentationKey、source revision、严格 UInk importer、索引发布或跨轨规则。
- 绿测：相同 standalone Debug|ARM64 Build exit 0、完整 Tests exit 0，红 2 项转绿；旧 A、新 B、主索引与未发布物理文件分别核验。日志：`TestResults/release-hardening/f041-r4-pending-green-build-debug-arm64.log`、`f041-r4-pending-green-debug-arm64.log`。本机测试没有模拟跨进程 B 恢复；fresh Service 正确读 A 是本事务边界。

## 独立测试合同清理｜待本轮最终回归

`Draw3.PresentationAutoSave.cppm` 的 `ShouldPersistLoadedPresentationBindingMigration` 只被 `presentation_autosave_tests.cpp` 中三条旧断言使用；第一条要求 page-index fallback→Stable 在原文件上持久化迁移，与用户选择的“旧文件保留，新 Stable 独立保存”相反。全仓调用搜索无生产调用，故删除该死 helper 和三条镜像断言；真实 `TestFallbackAndStableUseIndependentStorageTracks` 及 strict Save/Load 测试仍保留。此清理不改变运行时，不作为性能改进。改动后的 standalone Debug|ARM64 Build exit 0、沙箱外完整 Tests exit 0，原始日志 `TestResults/release-hardening/f041-r4-helper-cleanup-build-debug-arm64.log`、`f041-r4-helper-cleanup-debug-arm64.log`。构建日志中的 `Draw3.SpeedEraser.h` C4819 与 LNK4075 属于其它文件/配置告警，本单元未改其源码或构建设置。

## 限制

- 本单元只证明本机 Debug ARM64 生产 Storage 路径。最新完整 `InkeysRepo.sln` Debug/Release、Office GUI、Win7 SP1 + 仅 KB2670838 与实际断电仍由父任务另验；不能把 in-memory pending B 当作跨进程 durable commit。
- F044 禁用自动 GC 继续保留所有已知物理版本；额外磁盘容量和盘满路径沿用既有 P2 风险。本次没有测量性能收益。
- 修改的 `.cpp/.cppm`、standalone test 保持 UTF-8 BOM/CRLF，`git diff --check` 无报错；未提交、未启动 GUI。

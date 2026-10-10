# F041 Storage 独立复审修补：PreviousInterval 异常回执

日期：2026-09-28。实施范围：`Draw3.PresentationAutoSave.cpp` 的 worker Load 异常回执，以及 `presentation_autosave_tests.cpp` 的生产 Service 故障注入断言。独立审查来源为 `ppt-f041-storage-independent-review.md` 的 F041-R1。F041-R2/R3 在下文单独评估，本单元未修改其实现。

## R1｜P1｜已确认失败→standalone 已验证通过

- 触发：`PresentationAutoSaveService::Impl::Run` 处理 `PresentationLoadKind::PreviousInterval` 时，在 `LoadPresentation` 返回前抛异常。生产 worker 的 `throwWorkerOperation` 故障注入重现了该路径；一般工作项异常同样会走内层 `catch`。
- 根因：Load 回执预置只有 `target`、`slotGeneration`。异常 catch 将这份回执作为 `IoError` 发送时，`loadKind` 保持默认 `Current`，`pageGuid` 为空、`intervalOrdinal` 为 0。Controller 按种类处理撤销 Clear 的 PreviousInterval 回执，错误种类使 `intervalLoadPending` 留在 true。
- 红测：在现有 `TestWorkerExceptionBecomesCompletion` 中，对真实 Service 提交带 page GUID、ordinal 7、generation 89 的 PreviousInterval；断言 IoError 原样保留这些字段且 `storageTrack=Unresolved`、无 `fileGuid`。standalone `inkStrokeModelerTest.sln Debug|ARM64` 构建 exit 0，沙箱外完整 tests exit 1，唯一新增断言失败。原始日志：`TestResults/release-hardening/f041-previous-exception-red-build-debug-arm64.log` 与 `f041-previous-exception-red-debug-arm64.{stdout,stderr}.log`。
- 最小修复：在 worker 的 Load 分支、进入可能抛异常的操作前预置 `loadKind/pageGuid/intervalOrdinal`；保持 IoError，不伪装为 Loaded，也不改变三轨、版本索引或 Controller。
- 绿测：相同 standalone `inkStrokeModelerTest.sln Debug|ARM64` 构建 exit 0，沙箱外完整 `inkStrokeModelerTestTests.exe` exit 0；原 1 项红断言转绿。原始日志：`TestResults/release-hardening/f041-previous-exception-green-build-debug-arm64.log`、`f041-previous-exception-green-debug-arm64.{stdout,stderr}.log`。stderr 中的故障注入 `io_error` 是预期失败回执，不是进程失败。静态核对当前 `DrawingController.cpp:7327-7412`：回执为 PreviousInterval 时，失败安装且 pageGuid 匹配现存页会清对应 `intervalLoadPending`；这不是动态 Controller 通过证据。主 Solution、Controller no-HWND 清 pending 和 Office 真会话仍需独立复验；该 Storage 测试仅证明回执字段，不冒称完整 Controller 恢复已经动态通过。

## R2｜P2 条件兼容缺口｜本单元未改

旧 schema v1 容许合法大写 GUID 和对应旧路径，既有测试仅验证 Load。续存当前以字符串区分大小写比较 `found->fileGuid` 与规范小写 `FormatUInkGuid(snapshot.fileGuid)`，即使字节 GUID 相同也报 SourceChanged；直接使用旧大写文本写 v2 versioned 路径又不符合 v2 canonical 限制。第一方旧写入通常为小写，影响应标为条件性的旧数据兼容问题。

下一独立改动单元：扩展现有大写 v1 用例，真实 Service `Load→修改→Save→fresh Load` 先取红；按 GUID 字节比较身份，写新 v2 路径和索引时规范化 GUID 文本，同时验证旧 v1 UInk 原字节不变。不得放宽 v2 路径规则。

## R3｜P2 条件 API 缺口｜本单元未改

`PendingKey` 已含 generation，pending Load 也核 generation；但 `ConflictsWithOtherLogicalFile` 的 `sameLogicalFile` 比较未核该项。旧代 UInk durable、index 失败后，新代若在同 source/track 下复用 file/workspace GUID，可绕过跨代冲突并发布另一版本，旧 pending 留在内存。当前 Controller 新槽通常创建新的 GUID，尚无用户场景复现；这是可构造的 Storage API 条件缺口。

下一独立改动单元：用第一代 `failIndexCommit` 和第二代同 GUID、不同 generation 的真实 Service 保存先取红，预期第二代 SourceChanged 且旧 pending/文件保留；最小实现给 `sameLogicalFile` 增加 `std::get<5>(key) == request.slotGeneration`。若产品有明确跨代继承合同，需先在共享设计中定义唯一允许条件。

## 文件与边界

- 本单元只更改 `Draw3.PresentationAutoSave.cpp` 和 `presentation_autosave_tests.cpp`；`.cppm` 接口、F041 双轨/F044 物理版本/F048 known SlideID/禁用自动 GC 均未改。
- 两个修改文件保留 UTF-8 BOM、CRLF；`git diff --check` 无报错。
- 本轮无 GUI、真实 Office、Win7 SP1 + KB2670838 或 Release 运行证据；不可据此证明崩溃恢复或真实用户体验已通过。

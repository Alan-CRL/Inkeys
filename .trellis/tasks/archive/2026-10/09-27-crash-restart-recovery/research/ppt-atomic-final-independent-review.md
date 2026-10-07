# F-044 最终独立复审：版本化提交、禁 GC 与旧索引兼容

日期：2026-09-28。本人只读审查当前工作树的 `Draw3.PresentationAutoSave.cpp/.cppm`、`presentation_autosave_tests.cpp`、`contact_input_tests.cpp`，并追到生产 `uink_file.cpp`、严格 importer、Controller retained 保存来源、前一轮独立审查及 GC 收口记录。只写本报告；未修改产品/测试/父账本，未启动构建、GUI、性能或故障注入。四份目标文件的 `git diff --check` 为 0，均为原 UTF-8 BOM、CRLF。H0→工作树 diff 含前期独立改动，以下明确区分 F-044 变更与起点已有问题。

## 结论

F-044 的核心跨文件进程终止窗口在已执行的 T3 隔离场景内闭合：每次写新的 `files/<G>_<transaction>.uink`，`SaveUInkFile(CreateNewLogicalFileWithIdentity)` 自读与发布目标后返回 revision，随后才用 v2 临时索引替换主索引（`PresentationAutoSave.cpp:418-483,798-805,841-868`；`uink_file.cpp:921-991,1071-1104`）。旧主/备 index 所引用的旧 UInk 不再原位覆盖。生产文件中已无 `RetireUnreferencedVersions` 或已提交 UInk 的自动 GC 调用；`PresentationAutoSave.cpp` 的 `DeleteFileW` 仅留在本次索引临时文件失败清理（`:261,442,453`）。这处安全修补消除了上一轮审查指出的“读完旧版本关句柄、再按路径删除”的常规提交后竞态。

v1 固定路径继续按原文本 `files/<fileGuid>.uink` 读取；v2 只接受该固定路径或规范小写 GUID 的版本路径，拒绝 `..`、反斜杠、伪造 GUID/文件名前缀（`:289-341`）。`ReadIndex` 对 file GUID 与相对路径用 ASCII 大小写折叠作唯一键，挡住受测的 Windows 常见大小写同名别名（`:363-400`）。这些是**新二进制读旧索引**的兼容证据；旧二进制不保证读取 schema v2。

测试证据不是只看标题：`inkStrokeModelerTestTests.vcxproj:107,110,123-124,156-157` 把这些测试与同一生产 PresentationAutoSave/UInk 模块编进测试 EXE，`contact_input_tests.cpp:3000-3003,3051` 显式分流 child 后调用完整 `RunPresentationAutoSaveTests`，新断言登记于 `presentation_autosave_tests.cpp:1605-1611`。四项旧版本/index 断言先 red（`ppt-atomic-old-version-red-full.stderr.log`，总 exit 1），后一轮 GC/别名六项先 red（`f044-gc-alias-red-debug-arm64.stderr.log`，总 exit 1），最终 worker 记录的完整 standalone Debug|ARM64 Build 与隔离测试进程 exit 0（`f044-gc-alias-green-build-debug-arm64.log`、`f044-gc-alias-green-debug-arm64.{stdout,stderr}.log`）。最终 stderr 仍有设计内 index/UInk 故障注入的 `io_error` 行，不能当作整套失败；stdout 末尾 `All draw3 contact input tests passed.`。本人未重跑，退出码取自实施者执行记录而非日志末行推测。

真实隔离 child 只在生产 worker **新 UInk 已提交、索引尚未发布**的明确事件点暂停（`PresentationAutoSave.cpp:854-860`），测试以本次 `CreateProcessW` 返回的精确进程 HANDLE 调 `TerminateProcess`，验证旧 index/UInk 字节未变且 fresh Service 严格加载旧版本（`presentation_autosave_tests.cpp:794-919,1523-1553`）。这证明 T3 进程终止路径，不证明普通 UEF、15 秒监督器、断电、Win7、Office 或新进程可见画布恢复。

## 发现与需修正项

### R-044-F1 — P1，既存但本轮深审确认：首个已提交索引可漏记 retained SlideID

`ValidatePresentationSaveRequest` 允许有合法 `retainedCanvases`，逐项要求 retained 标志、SlideID 与页扩展（`PresentationAutoSave.cpp:575-586`）；Controller 的生产快照构造确实可携带 retained 页（`DrawingController.cpp:760,774,849,6162-6204`）。首次保存或原索引尚未形成而仅有同根 pending entry 时，`found==entries.end()` 分支把 `entry.slideIds` **仅**设为当前 `request.target.slideIds`（`PresentationAutoSave.cpp:730-742`），随后可把 retained 页写入版本化 UInk 并报告 index commit 成功（`:762-805,861-872`）。新进程/新 Service 严格加载时，`MakeExpectation` 把 index `slideIds` 当作 `knownSlideIds`（`:610-624`）；importer 对 UInk 中不在该集合的 retained SlideID 返回 `TopologyMismatch`（`uink_draw3_import.cpp:395-405`），`LoadPresentation` 因无 snapshot 返回 `Invalid`（`PresentationAutoSave.cpp:948-955`）。已有 importer 测试直接证明 known `{101,202}` 可保留 202，known `{101}` 拒绝 202（`presentation_autosave_tests.cpp:1330-1364`），但没有经过 Service 的首次保存→fresh Load 红测。

最小反例：当前 Stable target 只含 101，第一次持久化快照含合法 retained 202；Save 返回 Committed 的 index 只写 101，之后 Load 拒读。另一条是首个 UInk 写成 101/202 而 index 注入失败、期间删除 202，retry 从 pending canonical 合并 retained 202 后提交只含 101 的 index。前者不要求磁盘故障；后者会让先前可读的 pending 恢复点变成未引用孤儿。`entry.slideIds=request.target.slideIds` 在 H0 已存在，**不是 F-044 新回归**，但直接违反“最后已提交恢复点可严格读取”的发布底线。建议从已校验的本次将写入的 active+retained 内容生成 known SlideID 并与旧可信集合合并，确保每个 retained ID 进入 index；现有 `found` 分支的 `MergeSlideIds(found->slideIds,target.slideIds)`（`:849`）也必须包含本次新出现、尚未写入旧 index 的 retained ID。添加两个直接调用生产 Service 的红→绿用例：首次保存含 retained；首次 index 失败→拓扑删页/retained→重试成功→fresh strict Load。不要通过放宽 importer 为任意未知 SlideID 规避身份校验。

### R-044-F2 — P2，既存条件性 v1 大写 GUID 兼容缺口

修补后 v1 reader 明确保留单条大写 `fileGuid/relativePath` 可加载（`IsSafeRelativePath:292-294`，`TestIndexRejectsCaseAliasedFileIdentities:707-730`），但该测试没有后续 Save。`SavePresentation` 已找到 entry 的比较仍使用原文本 `found->fileGuid != FormatUInkGuid(request.snapshot.fileGuid)` 与 workspace GUID（`:815-820`），Load 的 workspace GUID 也按文本比（`:948-954`）。生产旧 writer 固定小写，未证自然生成大写索引；用户手改/迁移来的合法 v1 大写 `fileGuid` 可 Load、再 Save 返回 `SourceChanged`，大写 workspace GUID 则可在 decode 后 Load 变 `Invalid`。建议仅对 GUID 身份比较解析后的 16 字节、写新 v2 时规范化字段，同时保留 v1 原相对路径供旧文件读取；新增单条大写 v1 Load→Save→fresh Load，不做路径字符串粗暴小写以免影响 case-sensitive 卷。此限制已在 `ppt-atomic-gc-hardening.md` 记录，未修复。

### R-044-F3 — P2，索引其它 GUID 别名仍按字节判重

`DecodeEntry` 接受 `presentationKey/sessionId/workspaceGuid` 的大小写 GUID（`:317-333`），但 `ReadIndex` 只把 fileGuid/path 折叠，`keys.insert(entry.presentationKey)` 仍按字节（`:385-397`）。手工构造两条 source 不同、presentationKey 仅大小写不同的 entry 可通过结构校验，尽管解析为同一个 key；单条大写 key 又会在 `SavePresentation:698-700` 或 `LoadPresentation:927-935` 与 `FormatPresentationKey` 原文比较后被推迟/拒绝。实际生产 writer 都写小写，外部输入前提明确，尚无用户文件复现或跨文稿覆盖证据。最小修正是对 key 的唯一性用解析后的 GUID 值或 ASCII 折叠，并在 v1 单条大写兼容判定中按 GUID 值比较；测试须覆盖单条大写和大小写双 entry。不能把 file/path 的 alias 绿测扩大为所有 GUID 身份已规范化。

### R-044-F4 — P2，确定无界版本增长与磁盘满风险

禁止 GC 是正确的首发安全取舍，但每次成功 PPT 保存保留完整新物理 UInk；index commit 失败也留下未引用 orphan，`pendingIndexEntries[source]` 仅追最新一个（`SaveVersionedUInk:464-483`、`SavePresentation:861-871`），没有自动容量/保留上限。`TestPublishedVersionsAndUnknownFileAreRetained` 与三次提交计数验证了保留（`presentation_autosave_tests.cpp:753-793`）；未量化真实大文稿/长期使用增长、低余量磁盘和尾延迟。`Σ每次版本字节` 是增长模型，UInk 单文件读取上限 128 MiB 不是目录总量上限。短期不得恢复按路径 GC；应按隔离代表性 PPT 轨迹记录版本字节、提交频率、可用空间、失败孤儿和失败后旧主备 strict Load。若合理会话可耗尽磁盘，需安全容量策略或明确发布门禁。

### R-044-F5 — P2 条件性，旧有临时 index 路径清理仍存在对象替换窗口

禁 GC 后已提交旧 UInk 不会被本服务自动删除；但 `WriteNewTextFileDurable` 在关闭新建临时文件句柄后写失败按路径 `DeleteFileW(path)`，`CommitIndex` 在验证/替换失败后也按临时路径删除（`PresentationAutoSave.cpp:242-262,437-454`）。同权限外部进程若观察到随机临时名并能修改该目录，可能在句柄关闭与 DeleteFile 之间换入别的对象；named mutex 不约束外部进程。这个路径清理在 H0 已存在，且目标是随机本次事务 temp，故不可与已移除的常规旧版 GC P1 风险同等归因或宣称已验证提权；但“完全不删除未知对象”只能限定为**已提交版本**。最小修正是对失败临时文件仍持同一对象 HANDLE 完成删除，或不确定时保留 temp，不回到 `DeleteFileW` 的路径身份猜测；只在有真实可写目录/权限前提的隔离竞争测试中升级安全严重性。

## 独立发布边界与缺口

- 用户已决定“旧 page-index PPT 文件保留，新 StableSlideId 会话独立保存”。当前 `SavePresentation:809-850`、`LoadPresentation:921-933` 和旧测试 `TestPageIndexFallbackOverwrite:973-999` 仍允许按 ordinal 把同一 logical/index entry 升级 Stable；这属于**独立未完成的 F-041 P1 发布项**，不是 F-044 版本化事务已通过的证明。物理旧 UInk 虽保留，index 身份仍被复用，无法推出新旧会话隔离。
- `LoadIndexWithBackup` 仅在主 index 结构无效/缺失时取备份（`PresentationAutoSave.cpp:403-415`）。主 index 结构合法而其 UInk 损坏时，Load 返回 SourceChanged，不自动尝试 backup；不能把“备份仍在磁盘”写成“任何损坏都自动恢复”。
- T3 child kill 不覆盖临时文件半写 T1、`ReplaceFileW` 成功后 T5、部分失败/备份交换、磁盘满/拒绝写入、实际 F-043 15 秒强制终止、硬断电或用户可见重载；`CommitIndex:447-449` 的 `REPLACEFILE_WRITE_THROUGH` 既有标志不构成掉电耐久证明。Win7 SP1+仅 KB2670838 文件系统/Office/WPS、三架构 Release 与真实 GUI 均未由本 reviewer 验证。
- 任务规范 `draw3-integration.md:176,190,193,204-209` 仍记旧固定 `files/G.uink` 原位覆盖及 ordinal 升级，与当前代码和用户 F-041 决策冲突；由规范 owner 在 F-041 集成时同步。图形路径未改，Win7 FLIP/DComp/ULW 约束仍须独立真机测。

复审状态：F-044 版本化核心 **自动验证通过，仅限已执行的 Debug standalone/T3 进程终止**；自动已提交版本 GC **静态确认移除**；R-044-F1 **已确认静态调用链、待生产 Service 红测与修复**；F-041 **未完成**；容量/别名/临时删除 **条件性风险或未量化**；T1/T5/磁盘满/断电/Win7/Office/真实重启 **未验证**。不能写“无未处置 P1”或“发布就绪”。

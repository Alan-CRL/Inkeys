# F-044 独立复审：PPT UInk 物理版本与索引事务

日期：2026-09-28。角色：独立只读 reviewer。审查冻结时的 `Draw3.PresentationAutoSave.cpp/.cppm`、`presentation_autosave_tests.cpp`、`contact_input_tests.cpp` 实际 diff，UInk `SaveUInkFile` 的 create-new 路径、Controller 的保存触发、主备索引读写、F-041/F-043 决策与现有规范。未改生产/测试代码，未启动 GUI、编译或性能采样；`git diff --check` 本次读取时 exit 0，四个相关修改文件均仍为 UTF-8 BOM + 全 CRLF。

## 结论与已核证的边界

**F-044 核心进程终止窗口：在当前隔离测试覆盖的断点内成立；自动旧版本删除：尚不能放行。** 新保存使用 `files/<fileGuid>_<transactionGuid>.uink`，`SaveUInkFile` 的 `CreateNewLogicalFileWithIdentity` 分支先 create-new 临时文件、自读、`MoveFileExW` 发布目标、重读目标 revision；原索引所指文件不再原位覆盖（`PresentationAutoSave.cpp:453-470, 836-843, 879-885`；`uink_file.cpp:921-991, 1071-1104`）。`CommitIndex` 随后写 v2 临时索引并替换主索引；成功后新主索引与旧备索引分别指向不同物理文件（`PresentationAutoSave.cpp:407-443`）。这修复了先覆盖旧 UInk、再更新索引造成的已提交旧版本不可回读窗口。

路径解码要求 v1 仅使用 `files/<fileGuid>.uink`，v2 可用同一旧路径或规范小写事务 GUID 的版本路径，拒绝 `..`、反斜杠和不匹配 fileGuid（`PresentationAutoSave.cpp:281-323, 355-389`）。新 reader 可读 v1；新 v2 索引不能由旧二进制保证读取，故“升级兼容”仅为新读旧，不能写为双向降级兼容。`LoadIndexWithBackup` 仅在主索引结构无效/缺失时读取备索引（`:392-405`）；主索引结构有效但其 UInk 文件损坏时，`LoadPresentation` 返回 `SourceChanged`，不会自动取备份（`:979-987`）。这是目前的失败闭锁语义，不能把“备文件仍在”表述成“所有损坏场景自动恢复”。

红测日志 `TestResults/release-hardening/ppt-atomic-old-version-red-full.stderr.log` 有四个针对旧文件被覆盖及 fresh strict Load `SourceChanged` 的失败；独立 worker 报告 `f044-stage3-build-debug-arm64.log` 的 standalone Debug|ARM64 Build exit 0、`f044-stage3-standalone-debug-arm64.{stdout,stderr}.log` 的完整测试进程 exit 0。日志中还含设计内故障注入的 `io_error`，不能按单行错误误判整进程失败；本 reviewer 未重新运行。新测试由 `RunPresentationAutoSaveTests` 实际调用（`presentation_autosave_tests.cpp:1485-1490`）：失败索引后销毁旧 Service、fresh Service 读旧版本，第二次提交后损坏主索引读备份，手工 v1 fixture 迁移，非法路径拒绝，三次保存后的回收，以及真实隔离 child 在 worker 的 UInk 已提交/index 未发布断点被精确 `PROCESS_INFORMATION.hProcess` 强杀。child 入口是测试 EXE 显式参数，不在产品 EXE（`contact_input_tests.cpp:3000-3003`）。强杀测试验证了该一个 T3 断点和旧版本字节/严格 Load；不等于实际 15 秒 supervisor、UEF、Win7、Office、断电或新进程可见画布恢复。

## 需处理的发现（按严重性）

### R-044-1 — P1；已确认的路径删除竞态，外部触发条件未实测

`RetireUnreferencedVersions` 先用 `GetFileAttributesW` 检查目录/文件，接着 `ReadUInkFile(path)` 验证源 revision 与 header GUID，读取结束后句柄已关闭，最后以同一**字符串路径**调用 `DeleteFileW`（`PresentationAutoSave.cpp:475-515`）。`NamedMutexGuard` 只串行化遵守本协议的 writer；不能约束同权限外部进程、同步软件或文件管理器。在校验与删除之间，外部写者可替换候选文件，或在 `files` 属性检查后替换目录/junction。`DeleteFileW` 没有再次绑定刚验证的文件对象，会删除替换后的路径目标。这违反“不清理未知用户文件”；若进程有高于外部写者的文件删除权限且保存目录可被其重定向，影响可扩大。静态代码已证实 TOCTOU，尚无竞态复现或 ACL/提升前提证明，因此“可利用提权”仍是假设，不能直接宣称高危漏洞已被实测。

**首发最小安全方案：暂不调用自动版本 GC**，保留 v1 固定文件、v2 旧版和未知孤儿；只允许本次事务尚未发布且自己拥有的临时文件按现有路径清理。此方案不会改变主/备索引可读性，但会增加磁盘占用，需按 R-044-2 纳入门禁和资源验证。不要把现有 `GetFileAttributesW` 或再次 `ReadUInkFile(path)` 当作修复竞态。若选择保留自动 GC，必须在同一组打开的对象上验证并删除：以 `CreateFileW` 的 `FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT` 打开并锚定 `presentation`、`files` 目录，拒绝 reparse；持有拒绝 `FILE_SHARE_DELETE` 的目录/主备索引句柄，确认两份索引的严格引用集合；候选文件用 `GENERIC_READ | DELETE`、不共享写/删、`FILE_FLAG_OPEN_REPARSE_POINT` 打开，核 `GetFileInformationByHandle` 的卷/文件 ID、属性、完整 source revision/内容及 GUID，并用 `GetFinalPathNameByHandleW` 验证其最终路径属于锚定目录；只在**同一候选 HANDLE** 上调用 `SetFileInformationByHandle(FileDispositionInfo)`，任一不确定情况跳过。不能在句柄关闭后回到 `DeleteFileW(path)`。这些核心 API 的最低受支持客户端分别为 Vista 或更早，静态 API 合同不要求 Win7 SP1 除 KB2670838 外补丁；仍须 Win7 真机验证文件系统/网络卷语义。官方文档：[SetFileInformationByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle)、[GetFinalPathNameByHandleW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew)、[ReplaceFileW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew)。

安全 GC 的最小测试：隔离根中先真实三次提交形成一个可退休版本；在“校验后/删除前”显式暂停 worker，另一测试进程尝试原子替换候选文件、重命名 `files`、改为 junction、改动主/备索引；结果必须是替换/重定向被锁拒绝或 GC 跳过，绝不删除新对象或主/备引用。另测无外部竞争正常只留两代、损坏/未知/legacy 文件保留、Win7 SP1+仅 KB2670838 文件系统路径；不能以一个普通三次保存 PASS 代替竞态测试。

### R-044-2 — P2；确定的无界增长情形，容量影响依工作负载

即使保留当前 GC，**重复 index commit 失败**时，每次 `SaveVersionedUInk` 都产生新完整 UInk，失败分支只把 `pendingIndexEntries[source]` 更新为最新 entry，不清理前次未发布物理版本（`PresentationAutoSave.cpp:879-909`）；正常 GC 也只从旧备索引识别版本（`:475-497`），不会触及这些孤儿。长期权限/杀软/磁盘异常导致 index 写失败而 UInk 写成功时，孤儿数随尝试次数增长。停用 GC 作为 R-044-1 的最小修复后，正常已提交旧版也会累积。保存触发在 dirty 后的 Clear、换页、文稿/工作区切换、退出等安全点，写的是全量文稿，不是每帧或每输入样本（`DrawingController.cpp:5883-5909, 6852, 7142, 7263`）；因此增长约为 `Σ每次保存的 UInk 文件字节`，不能给固定“可保留 N 天”。举例：平均 20 MiB、50 次提交约 1 GiB；默认 UInk 读取上限 128 MiB（`uink_model.cppm:426`），大文稿少量提交即可占据数 GiB。实际发行用户的文稿大小/触发频率尚未测，故例子是容量估算，不是实测值或时限保证。

短期禁 GC 可优先消除破坏性删除 P1，但应把磁盘增长标为 P2，极大文稿/长会话或低剩余空间下可升级为发布阻塞。验证办法：隔离多页实际生产 PPT worker，记录每个 durable 版本的字节、每文稿每小时/日提交次数、失败索引次数、自由空间与保存排队/完成时间；以可用预算 `B`、平均版本大小 `S`、日提交数 `C` 估算 `B/(S×C)` 天，并测最坏 Clear/反复换页轨迹。不要自动删未知孤儿；若没有可证明的 Win7 安全 GC，就保留旧文件、提供准确诊断与容量门禁，不能默默恢复有竞态的删除。

### R-044-3 — P2；索引 GUID 大小写别名仍可通过严格解析（条件性数据风险）

`ParseUInkGuid` 接受大写十六进制（`uink_model.cpp:31-36, 60-76`）；`DecodeEntry` 对 `presentationKey/sessionId/fileGuid/workspaceGuid` 仅调用 parse，不要求 `FormatUInkGuid(parsed)==原文`（`PresentationAutoSave.cpp:317-320`）。v1/v2 `IsSafeRelativePath` 又用未规范化的 `fileGuid` 拼路径，Windows 常用文件系统的文件名不区分大小写；`ReadIndex` 的 `fileGuids`/`paths` 唯一集合与 GC `retainedPaths.contains` 却区分大小写（`:375-386, 492-502`）。因此手工编辑或外部写入的有效索引可含按字节不同、指向同一物理路径的大小写别名。生产 writer 固定输出小写，本 reviewer 未发现自然生成此别名的调用；这是有外部索引改动前提的条件性风险，未证明可从一般 Office 输入达成。建议 v2 reader 对所有 GUID 文本施加 canonical 检查；v1 兼容需先核历史数据/测试，再选择规范化比较或明确拒绝，至少 GC 的引用集合必须用 Windows 等价路径身份判断，不能只靠字符串集合。新增大小写别名 fixture，证明不会删除当前/备份实际引用。

### R-044-4 — P2；测试与耐久性主张需要收窄

真实 child kill 只在 `SavePresentation` 的 UInk 已 Committed、`CommitIndex` 之前暂停（`PresentationAutoSave.cpp:892-898`；`presentation_autosave_tests.cpp:674-760`）。T1 临时文件半写、T5 `ReplaceFileW` 成功后的强杀、`ReplaceFileW` 部分失败/备份移动、磁盘满/无权限、真实 15 秒 timeout 均未进行进程级故障测试。`CommitIndex` 中旧有 `REPLACEFILE_WRITE_THROUGH` 标志（`:435-441`）被微软当前 `ReplaceFileW` 文档标为“不支持”，不可据此证明掉电顺序/元数据耐久；已执行的进程强杀测试只支持“进程终止后可读”，不是断电保证。建议增加受控断点、文件字节/revision/主备 strict Load 检查，并把断电与 Win7 设备测试列人工，不扩大当前绿测结论。此项的标志与主备提交实现是 H0 既有路径，不归咎于 F-044 新增代码。

### R-044-5 — P2；规范与当前代码/用户决策冲突

`.trellis/spec/native-desktop/draw3-integration.md:176, 190, 204-205` 仍称固定 `files/<G>.uink`、`SaveExistingLogicalFile` 原位覆盖；`:193, 209` 仍允许旧 page-index 按 ordinal 升级 Stable。前者已被 F-044 实际 v2 写路径取代，后者已被用户的 F-041“保留旧文件、新 Stable 会话独立保存”决策否定；生产 F-041 尚未实施（`PresentationAutoSave.cpp:847-857, 962-974`）。必须由规范 owner 在 F-041 集成时同步修改，注明 v1 reader/v2 writer 与降级边界。F-041 是独立未完成发布项，不能用 F-044 通过来覆盖。

## 需要主任务继续验证的合同

- `CommitIndex` 主/备索引结构事务与 UInk 物理版本一一绑定的本机 Debug child kill 已有证据；完整 `InkeysRepo.sln Debug/Release|ARM64`、Win32/x64 Release、Win7 SP1+仅 KB2670838、Office/WPS、真实自动重启后的**可见**恢复均不在本 reviewer 执行范围。15 秒到期允许舍弃未 durable 的 worker 请求，旧已提交恢复点的测试结论只能写到已经打到的 T3 进程强杀点。
- 新 v2 reader 不应把主索引语法有效但 UInk 改坏时的 `SourceChanged` 伪报“自动备份恢复”。若产品要在这种情形回退，只能核备索引的同 session/key/source/G 与完整 revision 并返回明确恢复状态，不能扫描同 GUID 孤儿猜测最新。
- 正常保存每次新增完整版本；任何 GC/容量处理都不应在持有绘图 owner 上执行同步磁盘 I/O。当前 GC 虽在 worker 上，却在返回 Committed 前、持有 presentation 命名 mutex 时重新完整解码一个可达 128 MiB 的旧版本（`PresentationAutoSave.cpp:509, 912`）；在长文稿上应测保存尾延迟与等待队列，再决定同步清理是否满足退出性能。

复审状态：核心版本化事务 **已验证通过（限 Debug standalone 与 T3）**；自动 GC **已确认失败（安全竞态）**；容量/别名 **静态确认风险，实际影响未量化**；T1/T5/断电、Win7/Office、15 秒完整退出 **未验证/需要人工或后续隔离测试**。在 R-044-1 获得安全处理且 R-044-2 的容量风险入账前，不建议把 F-044 写为“无未处置 P1”。

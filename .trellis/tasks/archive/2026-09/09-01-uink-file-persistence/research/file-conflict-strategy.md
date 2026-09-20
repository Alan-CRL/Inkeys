# UInk 文件占用与外部冲突调查

## 1. 调查目标

为 `.uink` 的读取、完整保存和追加定义 Windows 7 SP1 可用的并发契约，避免同一进程、多个 Inkeys 进程或其他软件同时访问时出现撕裂读取、交错追加或最后写入者静默覆盖。

本文件只记录设计证据和已确认结论，不启动实现。

## 2. 仓库现有策略

`Inkeys/IdtConfiguration.cpp` 与 `Inkeys/Inkeys/Other/Other.Config.cpp` 使用了基本相同的占用逻辑：

- 读取以 `CreateFileW(GENERIC_READ, shareMode = 0, OPEN_EXISTING)` 打开；写入以 `GENERIC_READ | GENERIC_WRITE, shareMode = 0, OPEN_ALWAYS` 打开。
- 循环上限写作 5，但第三次失败就返回，所以有效策略是最多三次尝试、失败间隔 100 ms。
- 读写完成后立即 `CloseHandle`，不在配置使用期间长期占用。
- 写配置时先移动到文件头并调用 `SetEndOfFile`，随后原地 `WriteFile`。

对应证据：

- `Inkeys/IdtConfiguration.cpp:12-33`、`:39-63`：独占打开与重试。
- `Inkeys/IdtConfiguration.cpp:99-111`：同一句柄读取后释放。
- `Inkeys/IdtConfiguration.cpp:579-597`、`:787-805`、`:1126-1147`：原地截断并写入。
- `Inkeys/Inkeys/Other/Other.Config.cpp:30-93`：当前配置模块的占用辅助函数。
- `Inkeys/Inkeys/Other/Other.Config.cpp:865-918`、`:920-954`：短时读取与写入生命周期。

## 3. 可复用与不可复用部分

可以复用的原则：

- 使用 Win7 已支持的 UTF-16 Win32 文件句柄完成一次一致操作。
- 占用只覆盖实际读取或提交窗口，不默认覆盖整个编辑时长。
- 对短暂共享冲突做小范围、有界重试。

不能直接复用的部分：

- `shareMode = 0` 会连其他只读者也阻止；UInk 读取可允许其他读取者，同时拒绝写入和删除。
- 当前辅助函数不区分共享冲突、权限错误、路径错误和介质错误；UInk 只应重试明确的瞬时锁冲突。
- 原地 `SetEndOfFile` 会在随后写入失败时破坏原文件，不适用于完整保存。
- `bool` 返回值无法表达 `SourceChanged`、资源包不支持、恢复尾部或部分提交等状态。
- 当前策略没有记录加载来源版本，编辑数分钟后保存会覆盖期间的外部修改。

## 4. SourceRevision

读取成功时生成不可变 `SourceRevision`，建议包含：

- canonical path 的比较形式，仅用于定位目标，不作为内容版本；
- `BY_HANDLE_FILE_INFORMATION` 可提供时的 volume serial 与 file index；
- 文件字节长度和 last-write `FILETIME`；
- 对实际解析字节流增量计算的 SHA-256；
- Header GUID 作为 UInk 语义身份和诊断字段。

内容摘要应与解析发生在同一已占用句柄和同一遍读取中。Header GUID 可能被外部软件保留，时间戳和长度也可能碰巧相同，因此它们都不能单独承担版本判断；文件身份用于识别路径被替换，摘要用于确认内容。

## 5. 已确认方案 A：短事务占用 + 乐观冲突拒绝

### 5.1 Read

1. 以 `GENERIC_READ` 打开，并允许其他读取者但拒绝写入/删除。
2. 仅对 `ERROR_SHARING_VIOLATION`、`ERROR_LOCK_VIOLATION` 等明确瞬时错误进行有界重试；权限、路径、格式和资源限额错误立即返回。
3. 在同一句柄内取得身份、长度、字节流、摘要和末次写入时间。
4. 关闭句柄，将 `SourceRevision` 与完整 `UInkDocument` 一起交给 `UInkEditingSession`。

### 5.2 Full Save

1. 从完整编辑会话生成 save plan；含 Media 引用时先返回 `ResourcePackUnsupported`。
2. 在目标同目录生成并严格自检临时文件，尚不改变目标。
3. 取得路径级事务 guard，打开目标并比较 expected/current revision；不相等则返回 `SourceChanged`。
4. 对既有目标使用带唯一 backup 路径的 Win7 `ReplaceFileW`。提交后核对 backup 是否就是预期 predecessor；若不一致，报告提交期冲突并尝试恢复 backup。
5. 对原本不存在的目标使用不覆盖提交；如果提交时目标已经出现，按冲突返回。
6. 只有确认新目标与 save plan 一致、旧目标符合预期后才删除恢复材料并报告成功。

Win7 普通文件 API 没有按预期 hash 执行原子替换的 CAS 参数。路径级 guard 能串行化遵守本模块协议的进程，backup 核对负责识别最终窗口中不遵守协议的软件。若恢复失败或路径状态不确定，必须返回独立 partial-commit 状态并保留可读文件位置，不能假装原目标一定未变。

### 5.3 Append

1. `AnalyzeAppend` 固定完整 `SourceRevision`、有效前缀终点和预编码 batch。
2. `ExecuteAppend` 取得独占读写句柄并在该句柄内重算/复核 revision。
3. 同一句柄内依次执行可选尾部截断、完整 batch 写入和默认 `FlushFileBuffers`；只有显式 buffered 选项跳过刷新。
4. 写入失败时尝试回滚到 batch 起点；若只保留已批准的尾部修复则报告 `TailRepairedNoAppend`，回滚失败则报告 `PartialCommitRequiresRecovery` 和最后可信边界。完整写入后刷盘失败返回 `WrittenNotDurable` 和新 revision；后二者都不得被调用方当作未写入重试。

任何包含 Media 的 batch 都在开写句柄前拒绝。对于含 Media 的来源，batch 只能包含非 Media Ink/Shape/Canvas，并且只追加到原路径；既有 `.uink` 前缀和 `.uink.extra` 不打开、不修改。

## 6. 备选方案 B：编辑会话持续占用

从读取开始持续持有拒绝写入的句柄，直到文档关闭。优点是普通外部写入更早失败，应用内心智模型简单；缺点是可能数小时阻止其他软件保存、替换或管理文件，崩溃/休眠和 Save As 生命周期也更复杂。完整保存的路径替换仍需处理 handle share-delete、backup 和非协作进程，不能因此取消 revision 校验。

## 7. 确认结论

2026-09-01 用户确认采用 A。UInk 是交换格式，长期独占会明显削弱与其他软件协作；短事务配合强 `SourceRevision`、结构化 `SourceChanged` 和提交备份，在不静默覆盖的前提下更符合桌面文档行为。

同日确认 append durability 采用 A：默认执行 `FlushFileBuffers`，接受每次追加产生的少量延迟以换取更可靠的完成语义；显式 buffered 模式必须在结果中可见。完整 batch 已写入而刷新失败属于 `WrittenNotDurable`，不能笼统报告未修改或安全重试。

默认重试次数/时长和跨进程 guard 的具体实现属于后续工程验证，不改变已确认的行为契约；这些参数必须可在无窗口测试中注入，不能依赖真实 sleep 制造竞态。

## 8. 必测场景

- 读取期间另一个读者成功，写入者因共享规则失败；短暂占用释放后在预算内成功。
- 权限拒绝、路径不存在等非瞬时错误不重试。
- 加载后同路径原地改写、同长度同时间近似改写、原子替换为新文件均触发 `SourceChanged`。
- 两个会话从同一 revision 保存，只有先提交者成功，后提交者冲突。
- 两个 append plan 同源并发执行，后执行者 revision 失配且不重复编号。
- append 默认刷新成功、显式 buffered、部分写入回滚/回滚失败、仅尾部修复以及完整写入后刷新失败分别返回可区分结果；`PartialCommitRequiresRecovery` 提供最后可信边界，`WrittenNotDurable` 携带新 revision，重试保护测试不重复 batch。
- full save 在临时写入、自检、revision 复核、ReplaceFile 各阶段故障时保留至少一个可读版本。
- 非协作写入恰好发生在最终替换窗口时，backup predecessor 核对发现冲突并演练恢复失败结果。
- 含 Media 来源的非 Media append 不改变旧前缀或 companion；含 Media 来源的完整保存/Save As 和任何 Media append 在打开写句柄前拒绝。

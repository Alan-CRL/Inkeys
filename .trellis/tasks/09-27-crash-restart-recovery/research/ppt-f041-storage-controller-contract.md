# F-041 Storage ↔ Controller 冻结候选合同

日期：2026-09-28。**候选，待主 agent 与 Controller owner 确认后才改共享 `.cppm`。** 用户决定是保留旧 page-index 物理文件和索引，新 StableSlideId 会话用独立轨保存；不按旧 ordinal 迁移旧墨迹。F-048 known SlideID 一致性与 F-044 v2 新物理版本/禁自动 GC 是两轨都必须沿用的底层合同。

## 唯一共享接口

在 `Draw3.PresentationAutoSave.cppm` 定义 `enum class PresentationStorageTrack : uint8_t { Unresolved, Base, PageIndexSidecar, SlideIdSidecar }`，它描述**实际物理 index root**；逻辑 lane 仍取现有 `request/completion.target.bindingMode`。Base 可以包含任一 mode 的历史 entry，两个 Sidecar 名称为编译期固定目录。`Unresolved` 只用于 selector 尚未安全决定路径的失败回执，不能当成可保存根。

`PresentationSaveRequest` 与 `PresentationLoadRequest` 各新增 `uint64_t slotGeneration = 0`。这是 Controller 分配的不透明标签，0 允许旧测试/调用方逐步迁移；Storage 不用它授权文件、选择路径或猜同页身份。每一个已接受请求的终态（Committed、Loaded、NotFound、SourceChanged、CrossProcessConflictDeferred、Invalid、IoError，以及 worker 异常转换的 IoError）在 `PresentationPersistenceCompletion::slotGeneration` **原值 echo**。同步 Submit 的 Invalid/Closed 不产生 completion，由 Controller 立即处理拒绝。

`PresentationPersistenceCompletion` 另加 `storageTrack = Unresolved` 与 `optional<UInkGuid> fileGuid`：Save 的 `fileGuid` 总是该请求的 `snapshot.fileGuid`，包括 worker 后续失败；Load 只在 `Loaded` 且 UInk revision、应用严格 importer 和 workspace identity 均核准后带实际 entry/UInk GUID，其余状态为 null。`Completion.target` 仍完整回显原请求，`loadKind/pageGuid/intervalOrdinal/mutationRevision/clearPageGuid` 保持原字段合同；`PreviousInterval` 同样 echo generation、页 GUID、ordinal，迟到结果不能落到新 lane。`storageTrack` 在成功选轨后无论状态如何都回显所选根；无法解析 base/sidecar（index 损坏、双根同 mode 冲突等）时为 Unresolved。

## 轨选择与状态

所有 Save/Load 在同一个规范化 `<AutoSave>/presentation` named mutex 内读取 base 与两个固定 sidecar 的主/备 index 并选择根。优先选该 source/key 已有且 mode 相同的唯一 entry；若 base 对该 source 是另一 mode，当前 mode 选其固定 sidecar；两个根同 mode 同 source、source/key 不符或任一相关索引无法从主/备严格读取则 fail closed。历史 Stable base 仍留 Base；两处都空时选 Base。旧 fallback base 存在而 Stable sidecar 尚无 entry：Stable Load 返回 `NotFound/SlideIdSidecar/fileGuid=null`，Stable Save 用与 fallback 不同的 file/workspace/page GUID 在 sidecar 建新 v2 文件和 index；绝不从 fallback UInk 按 ordinal import。对称地旧 Stable base + 新 fallback 用 PageIndexSidecar。

同轨已有 entry 但 sessionId 是另一个进程时 `CrossProcessConflictDeferred`；另一 mode 的旧 base entry 不直接阻止本模式新 sidecar。新请求 fileGuid/workspaceGuid 若与另一轨已提交 entry 冲突则 `SourceChanged`，不靠随机 GUID 碰撞概率承诺隔离。base/sidecar 主备 index 结构无效且不能安全 fallback 返回 `IoError/Unresolved`，不扫描孤儿决定最新。选轨后不存在当前 mode entry 的 Load 为 `NotFound` 并 echo 所选 track。成功 Save 的 `Committed` 只表示新 UInk 与**该轨** index 已提交；失败仍保留该轨旧主/备恢复点与本次已知孤儿，不动另一轨。

Service 的 latest-wins 普通 Tail 只允许同 `target.key/sourceIdentity/bindingMode + snapshot.fileGuid/workspaceGuid + slotGeneration` 的请求互相替换；Clear boundary 仍 FIFO，跨轨请求必须各自产生终态。`pendingIndexEntries` 用 `(actual storageTrack, sourceIdentity, fileGuid, workspaceGuid)` 作键，且只在同一规范化 AutoSave root 的 Host generation 间延续；index 发布失败后的自写 revision 只修对应轨/文件。Controller 用 `(PresentationKey,bindingMode,slotGeneration)` 找同代 slot，Save 再核 fileGuid 与 mutation/clear 身份，Load 只在该 slot 仍 loadPending 且目标/页身份一致时安装；不能只凭同 key 吸入旧 fallback completion。

## 实施与验证门

先由 Storage owner 修改 `.cppm` 和 `PresentationAutoSave.cpp`，保留旧 API 默认值供现有调用点编译，用真实 Service 无窗口双轨/foreign/pending/同 GUID/队列交错红→绿。Controller owner只在上述字段语义经主 agent 确认后修改 mode lane、slotGeneration 生产/回执门、同键旧槽保留与退出/fatal 扫描；主 agent 唯一接 CLI 与串行 Solution/测试。两层联合后复验 F-029/F-031/F-038/F-039/F-048、EndScreen 与真实 Office/Win7 人工矩阵。

本候选未定义“每次相同 Stable 模式放映都新建文件”；当前用户决定只要求旧 page-index 与新 Stable 分离。同模式复用仍以现有 Stable SlideID/source/session 身份判断。若需每次放映新轨，必须另定持久身份与磁盘 schema，不能在本次接口里猜。

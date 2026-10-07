# F-041：保留旧 page-index，建立独立 StableSlideId 保存轨

日期：2026-09-28。用户已明确选择“旧 page-index UInk/index 保留，新 StableSlideId 会话独立保存”。本文件是 `PresentationAutoSave`、Controller 与测试的设计；F-044 已先实施 index v2/物理版本化，本设计下文按该新存储合同修订，F-041 产品代码仍未实施。未启动 GUI/Office/读取真实用户数据。跨 Inkeys 进程 PPT 自动恢复仍未开放。

## 当前结构为何不能直接插入第二条

`Draw3.PresentationAutoSave.cpp::ReadIndex` 对 schemaVersion 1/2 的 `entries` 均要求 `sourceIdentity` 和 `presentationKey` 各自全局唯一，`SavePresentation`、`LoadPresentation` 只按 source 找首条。同一路径的 fallback/Stable 共用 `TargetIdentityFor` 产生的 source/key；在同一 `presentation/index.json` 增第二条会让整个索引成为 Invalid。当前 Save 命中旧 fallback 后要求同 file/workspace GUID，fresh Stable slot 用新 GUID 会得 `SourceChanged`；沿旧 GUID 写又违反用户决定。简单把新 `.uink` 放进同一 `files/` 但不建独立可读索引，也无法完成新会话恢复。

## 推荐的最小磁盘分叉：同 schema 的静态 mode sidecar

新 Stable 轨不迁移或改写旧 fallback 索引。仍以 `<AutoSave>/presentation/` 为共同命名 mutex、读取和冲突判定根；旧 base `index.json(.bak)` 与其已引用的 UInk 版本完整保留。当 base 该 source/key 的已验证 entry 为 `page-index`，Stable 的新轨使用固定内部目录 `<AutoSave>/presentation/slide-id/index.json(.bak)` 与 `slide-id/files/<newGuid>_<txnGuid>.uink`；sidecar 复用 F-044 的 v1/v2 严格 reader、schemaVersion 2 writer 和每次新物理版本。对称地，若 base 已是 slide-id 而稍后出现需要独立 fallback 的新绑定，可使用固定 `presentation/page-index/`，防止未来又在反方向静默 `SourceChanged`。目录名为编译期字面值，绝不把 Office 路径/名称/bindingToken 原样拼进文件系统路径。

根选择必须在**同一个 base-root named mutex** 内按完整有效 base 与 sidecar index 决定：先选择该 source/key 已存在且 bindingMode 相同的轨；base 为相反模式而 sidecar 同模式缺失时选空 sidecar；两处同 source+同模式、key 不一致、任何索引主备均无效或旧文件身份不匹配时 fail closed，不猜测哪个可覆盖。已有发布过的 Stable base entry 继续走 base，以免本次代码让原 Stable 注释失踪。sidecar 与 base 各自的 index 仍保留独立 source/key 唯一规则，不需要 schema 2 或额外 registry 才能存同逻辑 source 的两种绑定；这两个目录共同构成一个产品 storage domain。若“每一次 Office 放映即使同为 Stable 也必须彼此独立”是用户决策的进一步含义，固定 mode sidecar 不够，需把绑定代次纳入新的版本化索引身份；本方案只保证**新 Stable 轨与旧 fallback 轨独立**，Stable 轨内部继续按当前 SlideID 身份复用。

`LoadPresentation` 对 Stable 请求只查选定 Stable 轨：旧 fallback base 存在、Stable sidecar 尚无 entry 时返回 `NotFound`（明确记录 `legacy_fallback_preserved`），绝不按旧 ordinal 导入/显示；sidecar 已提交时仍按现有 sessionId、key/source、source revision、严格 Stable importer 读取。fallback 请求仍只读 fallback 轨。旧跨进程 sessionId 冲突继续返回 `CrossProcessConflictDeferred`，不开放跨进程入口。`SavePresentation` 必须按同一轨选择、以新 `fileGuid/workspaceGuid` 创建**新的** Stable UInk 和 sidecar entry；若与旧 base 任一 GUID 相同要拒绝，不能靠随机碰撞概率承诺隔离。新轨以后按其自己的 file/source revision 写新物理版本，切 sidecar index 后保留其当前/备份引用；旧 base `index.json`、`.bak` 和旧 UInk 从 Stable Save 的第一步到最后一步均不作为写目标。

### 队列与失败恢复也必须按轨分开

当前 worker `pendingIndexEntries` 仅以 `sourceIdentity` 为 key；`SubmitSave` 普通 Tail latest-wins 只比较 `target.key`，同 key 的 Stable 请求可替换尚未 durable 的 fallback 请求。这两处必须改成包含 storage track（至少 bindingMode/root）与 fileGuid/workspaceGuid 的身份：同一轨、同一逻辑文件的普通 Tail 才能合并；Clear 仍 FIFO；跨模式/文件的已接受请求都保留终态。`pendingIndexEntries` 的 self-written revision 修复键也必须包含 track+source，以免 Stable index 失败后误修旧 base 或反向。`PresentationPersistenceCompletion` 建议携带提交的 fileGuid 与 track（至少 Save），让 Controller 把迟到 fallback completion 路由到原槽，而不只看同一个 PresentationKey。

同 base mutex 下，Stable branch 的事务复用 F-044：`CreateNewLogicalFileWithIdentity` 写新的 `<G>_<txn>.uink` → UInk durable/严格回读 → 在 sidecar 根 `CommitIndex` 原子替换。创建 branch UInk 失败不碰任一 index；UInk 已 committed 但 branch index 失败时保留该已知孤儿文件和旧 sidecar index，在**该 track** 的 pending revision 中记 self-written 值供下次验证收敛；不删除未知文件、不把 attempted 写成 durable。`CommitIndex` 只允许清理由该次事务创建的未发布 `.tmp`，版本回收只看同轨主/备/pending 的严格引用，不触碰另一轨。旧 fallback 在分叉前已经接受的 Save 可能继续合法更新旧 base；“旧字节保留”的验收应在先前请求达到明确终态之后取基准，要求后续 Stable 操作不再改旧字节。

## Controller 必须与 worker 同步分叉

不能只改 worker：当前 `CanUpgradePresentationBindingByOrdinal`/`CanReusePresentationDocumentSlot` 会对同 key/source 的 fallback→Stable 复用活动或 parked CPU document/history，且 `SetPresentationTarget` 可沿用旧 `activePresentationFileGuid/workspaceGuid`（`Draw3.Presentation.cpp:368-422`、Controller SetPresentationTarget）。用户选择禁止 ordinal 猜页，故自动 fallback→Stable 应成为明确的新 slot generation：先按已有 contact/命令边界封口并为旧 fallback 发出原轨保存请求；把其整组 document/history/retained/target/file/revisions 移到独立的 retiring-fallback slot，直到 worker 对已接受请求给出 Committed/Failed 终态；同 key 的新 Stable slot 则以 `TryCreateInkGuid/TryAppendBlankPage` 建全新 workspace/page GUID，`fileGuid` 保持空到第一次 Stable Save，retained 为空、mutation/queued/committed 初始一致，不展示旧 fallback 点。若创建新槽失败，保持旧活动槽与可见状态；不能先丢旧墨迹再尝试分配。

当前 `presentationSlots` 仅以 `PresentationKey` 为 map key，不能同时容纳同 key 的旧 fallback 与新 Stable；仅把 `CanReuse...` 改 false 会走 `topologyConflict`/isolated path，不会自动得到可保存的新 Stable。需要一组只用于待排空 legacy fallback 的独立槽（按旧 fileGuid/目标绑定代次唯一标识），其 Save completion、退出/fatal 最终扫描与失败重试均使用旧 track。新 Stable 仍占现有活动/key 槽。若旧 fallback 已无可见修改且最后一次保存确实 Committed，可直接释放；有 dirty、已接受但未完成或失败请求时必须保留，不能因异步快照已入队便推断 durable。迟到 fallback Load/Save completion 不得被 `CanReuse(fallback,stable)` 吸入新 Stable；当前已开放的 `PreviousInterval` 恢复也须按旧模式/页 GUID 留在原 legacy 轨或拒绝，不能越过代次。

Stable 进入时 worker `Load` 在 sidecar 缺席应明确回 `NotFound`，Controller 可用**新空 Stable 槽**打开同 target 输入；新笔迹的 first Save 因 root 分叉与 GUID 独立能 Committed，而不会先显示成功后落 `SourceChanged`。若 sidecar 已有当前 Inkeys session 的 Stable 文件，仍沿用严格 SlideID/EndScreen/retained 加载；其它 session 的文件保持现行冲突，不冒称恢复。真正新稳定 session 与旧 fallback 的身份事实须同时出现在 runtime 诊断：logical key/source 相同，`bindingMode/storageTrack/fileGuid/workspaceGuid` 不同。

## 直接触及生产路径的无 HWND 红→绿测试

**Worker/storage（`inkStrokeModelerTestTests/presentation_autosave_tests.cpp`，隔离临时目录）**：用真实 `PresentationAutoSaveService` 创建有两页不同 page GUID/点的 fallback UInk/index，`CloseAndDrain` 后记录旧 index、backup（若存在）、UInk 原始字节和 source revision。以**同 key/source、页数不变但相反 SlideID 顺序**的 Stable target `SubmitLoad`：旧代码错误 `Loaded`，新代码应 `NotFound`/明确独立轨，旧字节不变。再以与 fallback 不同的 file/workspace/page GUID 对新 Stable `SubmitSave`：旧代码 `SourceChanged`，新代码 Committed；严格 `SubmitLoad(Stable)` 只返回新 Stable 点和对应 SlideID，`SubmitLoad(fallback)` 仍返回旧 ordinal 点。分别注入 UInk 写入失败、index commit 失败与同 GUID 碰撞，断言旧三份字节不变、sidecar 上次有效 index 不变、已 durable 的 branch 孤儿按 track 收敛。用 worker 的 writeDelay 让 fallback/Stable Save 交错排队，断言两件请求均终态而非 latest-wins 跨轨替换。foreign sessionId 分别读两个轨都维持拒绝。测试不创建 Office/HWND，也不触碰真实数据。

**Controller/桥（`Draw3.DrawingController.cpp/.cppm` 的窄生产 scene-transition helper + 显式早期 CLI，或现有无窗口测试若能直接链接生产 helper）**：活动 fallback 两笔与 EndScreen → 同 key/source Stable `{SlideID 202,101}`，断言新 Stable 文档全空但 page GUID/workspace GUID 与旧不同、旧 fallback dirty/pending slot 和 GUID 保留到 worker 终态，新的首个 Stored Stroke 只写 Stable，旧 completion 不能覆盖新 slot。建立新 Stable slot 或 queue submission 失败时旧活动槽仍完整；完成后重复 target 幂等，不额外创建第三份 slot。旧 page-index 文件本身的值身份不得由测试手工赋到 Stable target 来“预设正确”。

**既有行为/构建门**：`InkeysHeadlessTests/presentation_descriptor_tests.cpp:142-160` 与 `inkStrokeModelerTestTests/presentation_autosave_tests.cpp:381-447` 明确期望同路径重开放映也可 ordinal 原位升级、单文件计数 1，须按用户新决策改为双轨身份、旧字节保留和新文件计数 2；记录行为差异，不能静默删断言。F-029/F-031/F-038/F-039、完整 PPT service/UInk、Debug/Release 原生 ARM64 Solution 回归后再做独立 diff review。Win32/x64 与 Win7 SP1+仅 KB2670838 的文件/线程/Office 组合仍需实际验证；本存储设计不改变 `FLIP_SEQUENTIAL`、DComp→ULW 或禁用的两种 DWM 路径。

## 文件所有权与实施顺序建议

1. F-044 产品修改/测试/独立复审冻结后，再由 worker owner 修改 `Draw3.PresentationAutoSave.cpp/.cppm` 与 `inkStrokeModelerTestTests/presentation_autosave_tests.cpp`：冻结 track 选择、sidecar 路径、queue/pending 键和 completion 身份，取得 service 红→绿；复用已确立的 v1/v2 reader 与 v2 writer，不能为 branch 绕过 strict ReadIndex。
2. 再由 Controller owner 修改 `Draw3.Presentation.cpp` 的自动复用判定、`Draw3.DrawingController.cpp/.cppm` 的 slot generation/retiring 路由与生产 no-HWND 入口；先冻结 worker 提供的 track/status/fileGuid 合同，不与 worker 同写文件。主 agent 唯一接 IdtMain CLI、维护父 finding/compatibility/validation 与串行 MSBuild。规范更新由主 agent 统一写 `draw3-integration`/PPT 持久化合同，明确旧选择的行为差异和 unsupported 跨进程范围。
3. 最后在相同隔离根目录执行 worker+Controller 联合无窗口链、严格回读和失败注入，再做完整 Debug/Release Solution 与独立 reviewer。真 Office 同路径新放映/页重排、末页/结束页、不同 Office 提供方、Win7 的 Hardware FL11.0 与无 FL11.0→WARP、ULW FLIP 及输入可见性仍必须列人工验收，不得以本机 CLI 结果宣称首发体验已达标。

**最小可逆回退**：新代码若分叉失败，保持旧 fallback 文件/index 与旧活动/retiring slot；新 Stable 请求应得到明确失败并阻止误以为已 durable，不能退回旧 ordinal 升级。撤销本单元时仅撤 branch root/路由和对应 Controller 分叉，不回退 F-029/F-031/F-038/F-039 或删除任何 sidecar/旧 UInk；已经产生的 sidecar 文件仍是用户数据，应保留待人工处理。

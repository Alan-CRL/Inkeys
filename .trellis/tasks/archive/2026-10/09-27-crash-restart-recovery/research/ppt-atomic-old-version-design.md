# F-044：PPT UInk 与索引跨文件提交的旧版本保护

日期：2026-09-28。只读研究/设计，未改产品、测试、规范或父账本，未构建、启动进程/GUI、执行磁盘故障注入。用户在 `exit-restart-deadline-decision.md` 已明确选择：正式结束/主动重启 15 秒到期可强制终止旧进程、允许损失尚未 durable 的 pending worker 请求；**此前已 durable commit 的 UInk/index 必须仍可严格读取**。本设计只保护后一条，不把 hard kill 称正常保存/UEF，也不开放跨进程 PPT 可见恢复入口。

## 当前失败窗口：R0/R1 与 index I0/I1 不是一个事务

记逻辑 PPT 文件 GUID 为 `G`，旧物理文件 `P=files/G.uink`。`IndexEntry` 的 12 字段中 `relativePath=P`、`sourceRevision=R0`；`R0/R1` 是 `UInkSourceRevision` 的长度和 SHA-256（`Draw3.PresentationAutoSave.cpp:265-342`），`I0/I1` 是 index.json 的前后版本。`ReadIndex` 只查 JSON/schema/路径语法，`LoadPresentation` 随后严格要求 UInk 文件 revision 与 entry 相等（`:348-395,805-879`）。

| 时间点 | 磁盘 index / 文件 / 临时备份 | 当前后果 |
| --- | --- | --- |
| T0，旧提交已完成 | `I0: G→P,R0`；`P` 内容 R0。可能的 `index.json.bak=I-1` 也指同一 `P` 的更老 revision。 | `SubmitLoad` 严格读 R0 正常。 |
| T1，新快照编码并保存到 P 的 `.tmp` | index 仍 I0，P 仍 R0，临时新内容 R1。 | 进程终止时旧组合仍可读；temp 是孤儿，不当最新。 |
| T2，`SaveUInkFile(SaveExistingLogicalFile)` 在 `uink_file.cpp:1012-1042` 以 `ReplaceFileW(P,tmp,unique .bak)` 替换；P 已 R1，旧 R0 在本次 `.bak`。 | index 仍 I0→P,R0，严格 Load 已返回 `SourceChanged`；虽然暂时有 backup，也没有 index 指向它。此时 hard kill 后可能仅有需人工找回的备份。 |
| T3，`SaveUInkFile` 验证目标 R1 后在 `uink_file.cpp:1095-1103` 删除其 `.bak` 并返回 Committed | index 仍 I0→P,R0，P 为 R1；R0 文件可能不再存在。 | 15 秒 helper 此刻强杀后，旧已提交 PPT 组合**不能**严格读回。现有 `pendingIndexEntries` 是进程内状态，硬杀后消失。 |
| T4，`PresentationAutoSave.cpp:785-791` 只把本地 entry 改成 R1，index temp 写入/flush/自读通过，尚未 `CommitIndex` | 磁盘仍 I0→P,R0，P 为 R1。 | 与 T3 同；index 自己的 temp 不构成提交。 |
| T5，`:792-802` 原子发布 I1 | 主 index I1→P,R1，backup index I0→P,R0；P 仍 R1。 | 主索引正常可读，但主索引以后损坏时 `LoadIndexWithBackup` 会选择语法有效的 I0，严格 UInk revision 不符，备份无效。 |
| T6，index publish 失败 | 磁盘仍 I0→P,R0；worker 记 `pendingIndexEntries=I1` 并返回 IoError。 | 同进程/同 root Host generation 可能用自写 revision 收敛；进程硬杀、新 Service/新进程丢 pending 后 `SourceChanged`。不能用现有“self-written pending”测试证明跨进程安全。 |

`presentation_autosave_tests.cpp:195-251` 的 index 失败测试使用**同一** `PresentationAutoSaveService` 重新 Start，保留 `Impl::pendingIndexEntries`，所以验证的是进程内收敛；`:267-334` 的 index 备份测试只在**第一次提交**后手工复制 index 并损坏主 index，尚未覆盖保存同一路径。现有两个测试都不会触发 T2–T6 旧版丢失。Desktop AutoSave 每次 `CreateNewLogicalFileWithIdentity` 新建不同路径，旧 index 仍指旧文件，其风险不同；本设计限定 PPT。

## 先红：生产 worker 的隔离断点与严格回读

1. 在 `inkStrokeModelerTestTests/presentation_autosave_tests.cpp` 的隔离临时根，固定同一 service session ID，先提交 A（page GUID GA，stroke x=10），得到 I0/P/R0，保存原 index 与文件字节。再提交同文稿 B（同 G/workspace、mutation+1、stroke x=20）。在**`SaveUInkFile` 已报告 Committed、`CommitIndex` 开始前**添加仅测试显式启用的 failpoint；该点必须位于真实 `SavePresentation` 分支，不能复制一份保存算法。第一层快速红测让 worker在此返回 IoError，并销毁整个 service 对象，创建**新** Service（不能复用同一 Impl/pending map），严格 `SubmitLoad`；当前预期为 `SourceChanged`，修补后必须读回 A 的 GA/x=10、R0。原 I0/P 字节应仍相同；新 R1 可以是孤儿，但不得替代旧指向。
2. 第二层做真实进程级红灯：`contact_input_tests.cpp::wmain` 增仅测试目标支持的 `--presentation-atomic-child`，父测试只在自己新建的子进程/临时根中启用生产 `PresentationAutoSaveTestFaultInjection` 的两个 event：worker 到达“UInk durable、index 未发布”后先 signaled-ready 再等待 release；父测试等 ready 后只对**这个 child HANDLE** 调 `TerminateProcess` 并等待退出，再在全新测试进程/Service 用同一 root/session ID 严格加载。不得通过进程名枚举、终止用户已有 Inkeys/Office；未收到 ready 时超时并安全清理本测试 child。正常生产默认 failpoint 关闭，普通用户无 CLI 能触发。
3. 新版还须隔离 T1（临时文件半写）、T2/T3（file commit 后 index 前）、T5（index 已发布而清理未完）、`failIndexCommit` 与主 index 损坏→backup：每个断点退出后，严格读取**主或受信任备索引指向的实际物理文件**并校验 entry 的 sourceRevision、file/workspace GUID、PresentationKey、SlideID/EndScreen、可见 stroke。绿灯需区分“旧 A 可读”与“新 B 已提交”；pending B 可丢，不能伪称 B durable。磁盘满/拒绝写入时文件/索引原字节不被覆盖。该测试是进程 kill，不是 UEF 崩溃路径，也不能证明断电持久性。

## 首选最小兼容方案：逻辑 G 不变，物理版本不可覆写，index 最后切换

### 保存流程

1. 继续由单 PPT worker 与规范化 root 的 named mutex 独占同一文稿保存；从**当前 index 指向的版本**按 sourceRevision 严格读回 canonical，再沿既有 `MergePresentationSnapshot` 保留 Clear/interval/retained/EndScreen 与 Tail latest-wins 语义。不要从目录扫描的孤儿猜测最新文档，也不从另一个 workspace/key 借 fileGuid。
2. 为每次提交生成唯一、受严格语法限制的物理路径 `files/<G>_<txnGuid>.uink`；`G` 仍为 UInk header 与 `IndexEntry.fileGuid` 的逻辑身份，`txnGuid` 只区分物理版本，Win32/x64/ARM64 均用 ASCII 小写 GUID。新文件用现有 `UInkSaveMode::CreateNewLogicalFileWithIdentity` 创建/flush/严格自读，**不再对当前 index 的 P 做 SaveExistingLogicalFile**。该 UInk mode 在 `uink_file.cpp:559-660,965-992` 保留 caller 指定的非零 header GUID 并只对不存在的目标作 create-new；故无需改通用 UInk codec 或 HLSL/COM ABI。撞名重新取 GUID，异常/创建失败保持 I0/P0 原样；不清理未知文件。
3. 在 `IndexEntry` 中将 `relativePath` 指向新 P1、`sourceRevision` 指向 R1、`mutationRevision/knownSlideIds` 按现有合同更新，再使用 `CommitIndex` 的完整新 JSON temp、flush、自读与 `ReplaceFileW`/`MoveFileExW` 最后发布 I1。到发布成功前，I0 总指仍未修改的 P0/R0；发布后 I1 指 P1/R1，`index.json.bak` 的 I0 指仍存在的 P0/R0。若 index publish 失败，新 P1 保留为已知孤儿、旧 I0/P0 不动，返回 IoError；同进程 `pendingIndexEntries` 可在严格验证后尝试收敛，但**跨进程安全不依赖它**。
4. 更新物理路径策略，而非只放宽字符串 contains：现 `DecodeEntry` 强制 `relativePath=="files/"+fileGuid+".uink"`（`PresentationAutoSave.cpp:288-324`）。建议新 index `schemaVersion=2`，reader 同时接受 v1 旧固定路径与 v2 的严格 `files/<G>_<txnGuid>.uink`（v2 中未写过的其它文稿 entry 可仍是旧路径）；拒绝斜杠变体、`..`、额外扩展名、非法 GUID/大小写歧义，且路径/逻辑 GUID 与 entry 绑定。首次从 v1 保存时只把**新物理版本**写入 v2 index，原 `files/G.uink` 保留给 v1 backup；旧安装文件无需搬迁。新二进制能读旧 v1 主/备。旧二进制按原严格 path/schema 未必能读新版 v2；这属于**降级兼容**限制，不能宣称双向兼容，须在发布检查说明。
5. 每次 commit 后，先严格复核新的主/备索引及其全部引用文件，再仅对**已从受信任的旧主/备索引知道、现在不被任一有效主/备/pending 引用**的旧应用版本考虑删除；删除前还要核实际 header G、sourceRevision 与已记录值，用户修改/未知文件一律保留。第一次 v1→v2 commit 必须保留 legacy P0；第二次 commit 后 backup 通常指 P1，P0 才可能退休。删除失败只留已知孤儿，不回退成功的 I1。进程在 cleanup 前被杀也只是多一个旧版本。为了不无界增长，正常连续保存应维持主+备两代及少数故障孤儿；跨进程未知孤儿的自动 GC 另需所有权证明，不能用 `remove_all` 扫整个 `files/`。

### 断点不变量与严格读取

| 断点 | 允许的新状态 | 必须仍可读取的旧 durable |
| --- | --- | --- |
| P1 创建/写/flush/自读前后、尚未 I1 发布 | P1 不存在、临时文件、或完整孤儿 P1 | I0→P0,R0；P0 原字节未动。 |
| I1 temp 写/flush/自读后、index Replace 前 | P1 完整但 index 仍 I0 | I0→P0,R0；读路径不扫描/误收 P1。 |
| index Replace 成功后 | I1→P1,R1；backup I0→P0,R0 | I1 与 P1 严格配对；主索引坏时 backup I0/P0 仍严格配对。 |
| 退休版本清理中 | 仅删不再被主/备/pending 索引引用且校验为本服务版本的更老文件 | 当前 I1/P1 与 backup I0/P0 均不得触碰。 |

`LoadPresentation` 继续先核 session/key/source/binding、再从 entry.relativePath 读该版本、比对 sourceRevision、执行 `ImportApplicationOwnedPresentation`；若 v2 主 index 语法有效但引用文件损坏/丢失，不能偷偷读任一同 GUID 孤儿当最新。可仅在**可信备 index** 的相同 source/key/session/G、文件与 revision 均严格匹配时返回明确 fallback/recovery 状态，不能静默覆盖主 index。强杀任一进程时点后，至少旧 I0/P0 或已发布 I1/P1 应在；真正断电/硬件写缓存重排、磁盘满与 Windows `ReplaceFileW` 失败仍需专门设备/故障验证，不能由进程级 kill 证明。

## 次选候选与不取的捷径

- **把 `.bak` 留到 index commit 后、加载时按 R0 找备份**：仅给 UInk `SaveExistingLogicalFile` 加 `preserveBackup` 不够。主 index I1 指同一路径 R1、备 index I0 指同一路径 R0；以后再次保存会生成另一备份，路径和存活期需要与各代 index 精确关联，不能扫描未知 `.bak` 猜哪个属于 I0。若最终给每版备份稳定命名/索引关联，等价于版本化物理文件且改动更绕。
- **先写 index 再覆 UInk、忽略 revision mismatch、只保存 checksum、直接重试旧 index**：分别会制造“索引指未发布文件”、跨版本错读/错页、丢失严格来源检查或覆盖旧 valid。都不满足用户已提交可读与安全边界。
- **仅禁用 15 秒 watchdog 于 worker 写入阶段**：可以减少被本任务强杀击中 T2–T4，但用户已授权 worker 挂起时也必须有限终止；普通 crash/断电仍可命中该窗口，不是 F-044 根治。

## 影响文件、验证和发布状态

首选实现主要改 `Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cpp` 的 index v1/v2 读写、物理路径生成、Save/Load/pending/有限清理与失败路径；`.cppm` 仅添加默认关闭的测试 fault hook/诊断字段（若能放 cpp 内不扩大 module API则更好）。测试改 `inkStrokeModelerTestTests/presentation_autosave_tests.cpp`，隔离进程入口才触及同目录 `contact_input_tests.cpp`；现有 `CountUInkFiles==1`、固定 `files/G.uink` 的断言需按主/备两代与 v1 迁移调整，不能为了绿灯删掉备份恢复/foreign session/root-change/Boundary FIFO 用例。同步修订 `.trellis/spec/native-desktop/draw3-integration.md` 中“固定 `files/<G>.uink`、原位覆盖、index 失败保持文件”合同，并记录本次磁盘 schema v2 与旧版降级限制。无需改 PptCOM/TLB、CPU/HLSL ABI、Win7 API 或最低系统；沿用当前 Win7 可用的文件/命名互斥体/原子替换链，仍须 Win32/x64/ARM64 Release build 与 Win7 SP1+仅 KB2670838 实际文件测试。`FLIP_SEQUENTIAL`、仅 DComp/ULW 的图形选择均不受此磁盘修补影响。

最小发布证据：同一个生产 worker 的 failpoint 红→绿（全新 Service 与实际 child kill 两种）、旧 v1→v2、覆盖保存后备 index 严格回读、T2/T3/T5 每断点的索引/文件字节与 revision、失败不删除未知文件、普通 PPT 多页/EndScreen/retained/清空后重入、同 session Host restart 与 foreign session 防串、索引源版本和同根 pending 迁移。完整 `InkeysRepo.sln Debug|原生架构`、Release 及对应 UInk/PPT no-window 测试独立记录；没有真实 Office/磁盘故障/Win7 时保持需要人工，不能把主 Solution 编译通过当最后已提交恢复 PASS。15 秒 watchdog 在 F-044 红灯未转绿前不应宣称满足“旧 durable 可读”的发布底线。

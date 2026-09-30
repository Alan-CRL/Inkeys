# F-041 Storage 最终补丁独立复审

日期：2026-09-28。范围：只读检查 `Draw3.PresentationAutoSave.cpp/.cppm`、`presentation_autosave_tests.cpp` 的实际工作树 diff，并沿 `Draw3.DrawingController.cpp` 的生产调用/回执路线交叉核对。依据 `ppt-fallback-new-session-design.md`、`ppt-f041-storage-controller-contract.md`、`ppt-fallback-storage-f041.md` 与 F-044/F-048 测试记录。本轮没有修改产品或测试，没有启动构建、GUI 或性能采样；以下绿灯是核查主 agent/实施者留下的既有原始日志，不是本 reviewer 重跑。

## 结论与需处理项

### F041-R1｜P1｜已确认：PreviousInterval 异常回执丢请求种类，清屏撤销可能卡在 pending

- 证据：worker `Draw3.PresentationAutoSave.cpp:1321-1355` 对 Save 预填完整路由字段，却对 Load 只预填 `target`、`slotGeneration`。`LoadPresentation` 的局部回执本会填 `loadKind/pageGuid/intervalOrdinal`（`1077-1083`），但函数抛异常时尚未赋回 worker；内层 catch 将 worker 预填对象作为 `IoError` 发出，`loadKind` 仍是默认 `Current`，页 GUID 为空、ordinal 为 0。`SnapshotTestFaults().throwWorkerOperation` 是可重复注入点；正常 I/O、JSON、分配异常也可走同一路径。现有异常回归 `presentation_autosave_tests.cpp:2121-2133` 只提交 `Current` Load，所以没有覆盖这个字段合同。
- 实际影响：Controller `Draw3.DrawingController.cpp:7727-7740` 为撤销 Clear 提交 `PreviousInterval` 并置 `runtime.intervalLoadPending=true`。回执以错误的 `Current` 种类到达后跳过 `7327-7412` 的 interval 清理，进入 `7414-7431` 的整页恢复失败分支；该分支只清活动文稿的 load pending、把持久化初始化标记为 false，没有清该 runtime 的 interval pending。后续 `7717-7719` 因 pending 仍真而不再发相同撤销请求，直到槽重建等外部状态变化。用户报告的输入/清屏异常使这一条件缺陷有发布优先级，但不能据此断言它就是报告中画布卡死的唯一根因。
- 最小修复：在 worker 的 Load 预填分支同时复制 `item.load.kind/pageGuid/intervalOrdinal`，让任何异常终态原样 echo；不要在 catch 里把失败伪装成成功加载。直接在真实 `PresentationAutoSaveService` 用 `throwWorkerOperation` 提交 `PreviousInterval`，先取得红灯，再核 `IoError + PreviousInterval + 原 pageGuid/ordinal + generation + Unresolved + 无 fileGuid` 绿灯。另用现有 Controller no-HWND 生产探针核失败回执会清该页 `intervalLoadPending`，避免只测字段。该修正应优先于最终发布门禁。

### F041-R2｜P2｜早期遗留兼容缺口：合法大写 GUID 的 v1 index 可读却不能续存

- `presentation_autosave_tests.cpp:826-859` 明确构造合法单条 uppercase GUID v1 entry，证明当前 `Load` 返回 `Loaded`；但没有在该 Load 后进行 Save。续存时 `Draw3.PresentationAutoSave.cpp:1010-1015` 用大小写敏感文本比较历史 `found->fileGuid` 与 `FormatUInkGuid(snapshot.fileGuid)`（canonical 小写），直接报 `SourceChanged`。即使跳过此比较，`290-303` 的 schema v2 versioned 路径只接受小写 canonical fileGuid，不能把原大写值直接写入 v2 新路径。
- 这是 F-044/v1 迁移层早期已发现的条件性问题；第一方写出的旧文件通常是 canonical 小写，不能夸大为所有旧 PPT 都无法保存。建议补合法大写 v1 `Load→修改→Save→fresh Load` 的生产 Storage 回归，按 GUID 字节比较身份，写新 v2 索引/版本路径时规范化 GUID 文本，同时保留旧 UInk 物理文件字节不动。

### F041-R3｜P2 条件性 API 合同缺口：pending 的跨代 GUID 冲突未按 generation 判定

- `PendingKey` 已包含 `slotGeneration`（`493-505`），Load 的 pending 匹配也核代次（`1107-1125`）；但 `ConflictsWithOtherLogicalFile` 对 pending 的 `sameLogicalFile`（`642-650`）只核 track/mode/source/fileGuid/workspaceGuid，未核 key 的第 6 项 generation。若旧代首次 UInk 已提交而 index 故障，随后新代以同 file/workspace GUID 对同 source 提交 Save，跨代 pending 会被当作同一逻辑文件而放行；新 Save 可建立另一物理版本及 index，使旧代的未发布恢复身份留在内存。
- 当前 Controller 新建槽先创建新 workspace，首次 Save 再创建 fileGuid（`DrawingController.cpp:475-498,935-952`），正常随机 GUID 重用并未被证明可达。因此这是已确认的 Storage API 条件缺口，尚不是已复现的用户数据串页。最小规则是 `sameLogicalFile` 还需比较 `std::get<5>(key) == request.slotGeneration`；用第一代 `failIndexCommit` 与第二代复用 GUID 的隔离测试明确预期为 `SourceChanged`，并证明旧 pending/旧物理文件仍保留。若设计允许跨代显式继承同 GUID，应先把其唯一合法迁移条件写入共享合同并增加相应测试。

## 合同逐项核查

| 项目 | 静态结论与证据 | 状态 |
|---|---|---|
| 三轨 selector | `SelectStorageTrack`（`522-625`）在同一 base named mutex 内读 Base/SlideIdSidecar/PageIndexSidecar；同 source+mode 的 Base/sidecar 双权威及 pending/committed mode 矛盾均 fail closed。旧 fallback Base 存在时，新 Stable 选固定 slide-id sidecar；历史 Stable Base 保持 Base，新 fallback 选 page-index sidecar。 | 静态已验证；standalone 场景已通过 |
| 旧 fallback 主/备与 UInk 字节 | Stable Save 走 sidecar，版本名由 `CreateNewLogicalFileWithIdentity` 新建；仅选定根的 index 被 Commit。测试 `1121-1269` 保存旧 Base 主/备 index 和两份 UInk 的字节，Stable Save 前后逐一比对。此新路径未见删除旧 Base UInk；`DeleteFileW` 仅用于本事务新建失败的临时文本/索引文件。 | 当前测试已验证；磁盘断电/目录重解析未验证 |
| 独立身份与 EndScreen | 新 Stable 首存带独立 file/workspace GUID，跨轨 GUID 冲突 Save 返回 SourceChanged（`627-652,847-848`）；`TestFallbackAndStableUseIndependentStorageTracks` fresh 双轨 Load 比对 page GUID/ink/mode。F-048 known SlideID 根据合并后的 active/retained 建立，EndScreen 不加入 SlideID 集合（`887-920`）。 | 静态与 standalone 已验证；Office 真会话未验证 |
| foreign/root/pending | selector 对同 source/key 不一致拒绝，found entry session 不同拒绝；pending key 含实际 track/mode/source/file/workspace/gen，Load 仅同轨同代使用。`Start` 同根保留 pending、换根清除（`1385-1410`）。测试 `1348-1450` 证同根 pending 可恢复、fresh Service 看不到未发布 sidecar、foreign 双轨拒绝。 | standalone 已验证；跨进程异常退出未由本轮新增实测 |
| latest-wins/Clear | `SubmitSave`（`1429-1458`）仅同 key/source/mode/file/workspace/gen 的普通 tail 替换；遇同 key 任一 Clear 停止回扫。worker 单队列保持接受次序。跨轨 event 暂停测试 `1272-1346` 核两项 Save 均获终态。 | standalone 已验证 |
| completion echo | Save 的异常/失败先预填 target/gen/fileGuid/revision/Clear（`1324-1333`），选轨后 `storageTrack` 回显；Load 的非抛异常路径有 gen/track/严格 Loaded GUID，失败不带 GUID。**异常 `PreviousInterval` 缺口见 R1**。`PushCompletion` 分配失败会记录日志而丢回执（`1283-1305`），既有合同已承认该资源极端边界。 | 部分已验证，R1 确认失败 |
| Controller 生产调用 | 新建 lane 赋非零代次，生产 Save 在 `BuildPresentationSaveRequest`/`submitPresentationSlot` 传 generation；重试/初次/current/PreviousInterval Load 均设置 generation（`7494-7499,7554-7559,7730-7740`）。Controller 接收以 lane+generation 路由、Save 加 fileGuid/revision、Load 核 track/mode/GUID。R1 指出错误 loadKind 会绕过正确分支。 | 静态已核；需 Controller 独立审查与 no-HWND 联合回归 |

## 既有红绿证据与限制

- `TestResults/release-hardening/f041-storage-red-debug-arm64.stderr.log:29-38` 有 8 项预期 `[PresentationAutoSave] failed`；实施者报告该轮 `inkStrokeModelerTest.sln Debug|ARM64` 构建 exit 0、测试 exit 1。检查了原始红日志，失败围绕旧 fallback 错误升级、改写 Base、没有 sidecar 与 fresh 双轨不一致。
- Stage2/Stage3/Stage4 构建日志均有 `inkStrokeModelerTestTests.vcxproj -> ...ARM64\Debug\inkStrokeModelerTestTests.exe` 产物；各 stdout 无 `[PresentationAutoSave] failed`，实施者/主 agent 记录完整进程 exit 0。Stage3 补 worker 停在 UInk 已提交与 index 切换之间的跨轨排队、pending、foreign、异常回执；Stage4 补 Stable Base 优先及反向 PageIndexSidecar。现有日志证明**独立 Draw3 测试进程**，不证明 Office/真实 GUI、Win7 SP1+KB2670838、断电或 Release 矩阵。
- `git diff --check` 对本轮三文件无报错。F-044/F-048 保留不可回收版本的设计避免删除未知用户文件，但长期版本增长/盘满仍为已知 P2 资源门禁；目录 junction/reparse 与原子替换的物理边界需按父任务安全审查记录。没有根据单次测试宣称数据恢复或主观体验已通过。

## 建议的最小复验顺序

1. 先修 R1，取 `PreviousInterval` 注入红→绿与 Controller 清 pending 证据；随后串行完整 `InkeysRepo.sln Debug|ARM64`、Headless/no-HWND 和 standalone 相关用例。
2. 对 R2/R3 各加一条真实 Storage 路径回归后做最小修正；若 R3 被明确限制为不可达的内部 API 输入，须在合同和验证中写清该限制，不能仅用随机 GUID 低概率代替身份证明。
3. 最终 Office 真机验证同 key fallback→Stable（SlideID 重排）、Stable→fallback、末页与 EndScreen、退出再进/保存失败重试；将旧 Base 主/备/UInk 字节证据与新 sidecar GUID 分开留存。Win7/Release 与 GPU 组合按父任务矩阵另验。

## 增量独立复审：R1/R2/R3 修补后（2026-09-28）

本节以当前工作树和 `ppt-f041-storage-review-fixes.md`、`ppt-f041-storage-r2-r3.md` 的新日志为依据，**取代上文关于 R1/R2/R3 尚未修复的旧阶段状态**；其它跨层与真机门禁不变。本 reviewer 仍只读，没有重新编译或修改产品/测试。

| Finding | 最新代码与直接测试 | 增量结论 |
|---|---|---|
| R1 PreviousInterval 异常字段 | worker `Draw3.PresentationAutoSave.cpp:1341-1348` 在内层故障注入、`Save/LoadPresentation` 调用之前预填 `loadKind/pageGuid/intervalOrdinal`，同时保留 target/generation；内层异常在 `1364-1366` 转 IoError。真实 Service 测试 `presentation_autosave_tests.cpp:2316-2334` 用 PreviousInterval/pageGuid/ordinal7/gen89 断言全部 echo、track Unresolved、无 fileGuid。原始 `f041-previous-exception-red-debug-arm64.stderr.log` 恰好 1 个相关 FAIL，green stderr 0 个；实施者/主 agent 记录 standalone Debug|ARM64 Build exit0、红 tests exit1、绿 tests exit0。 | **Storage 目标触发已修复，standalone 已验证通过**。Controller `DrawingController.cpp:7327-7412` 的实际处理会按 kind 进入 interval 分支，失败时按 pageGuid 清 pending；但未有本增量独立运行的 Controller no-HWND 端到端证据。 |
| R3 跨代 pending 同 GUID | `PresentationAutoSave.cpp:640-650` 的 `sameLogicalFile` 现明确核 `std::get<5>(key) == request.slotGeneration`。真实 Service 测试 `presentation_autosave_tests.cpp:1578-1632` 让 gen11 UInk durable/index 失败、gen12 同 GUID Save 预期 SourceChanged，断言没有 index/第二 UInk、旧 UInk 字节不动，并让 gen11 pending 严格 Load。原始 `f041-pending-generation-red-debug-arm64.stderr.log` 2 个指定 FAIL，green 0 个；构建与进程退出码按实施记录为 0/红1/绿0。 | **已修复，standalone 已验证通过**。不把构造型 API 边界称为已在用户会话复现，也不把它外推为所有跨代重用情形。 |
| R2 uppercase file GUID v1 续存 | `PresentationAutoSave.cpp:1012-1018` 把旧 file GUID 解析为值后与请求值比较；`1041-1050` 仍从旧 `relativePath` 严格读取，写新版本时使用请求的 canonical GUID，并把新 v2 index 同步规范化。真实 Service 测试 `presentation_autosave_tests.cpp:881-1005` 先 Load 旧墨迹，再 Save 清空，核 Committed/Base/gen21、v2 规范路径、旧 UInk 字节不变、旧 **main index 字节成为新 backup**、fresh Load 见新空页。初始红 fixture 又把 workspace 文本大写，先失败在旧 Load，**不算可信 R2 红证**；修正 fixture 后 `f041-uppercase-v1-red-fixed-debug-arm64.stderr.log` 唯一 Save FAIL，green 0 个。 | **针对 file GUID 大写、workspace 规范的合法 v1 情形已修复，standalone 已验证通过**。同轨正常续存本来就会切换 main index；证据是旧 main 由 backup 保留、旧 UInk 不变，不是声称新 main 与旧 main 字节相同，也未证明已存在 backup 会永远原样保留。 |

### 保留的边界与新发现

- **R1 “任何 throw 前”措辞需限缩。** 上述字段预置位于 worker 内层 `try` 前，但 `completion.target = item.load.target`（`PresentationAutoSave.cpp:1343`）本身先于 `loadKind/pageGuid/ordinal`，若字符串复制等分配抛异常，会落入 `1327-1387` 的外层 catch，仅写日志而无回执。`PushCompletion` 分配失败也可能丢回执。这是原报告已承认的极端资源边界，不可将这次 fault 注入绿灯写成“任何异常都必有 completion”；没有证明这种 OOM 路径在普通运行可复现或是 F041 新回归。若父任务要求 accepted 请求绝对终态，需要单独设计无分配回执/故障通道，不宜在此修补中随意扩大。
- **R2 仍有条件性 P2：uppercase workspace GUID v1。** `DecodeEntry` 只要求 `ParseUInkGuid(entry.workspaceGuid)` 成功（`PresentationAutoSave.cpp:328-334`），故单条 v1 大写 workspace GUID 可通过语法；但 `LoadPresentation:1179-1183` 以 `FormatUInkGuid(imported.snapshot->workspaceGuid) != found->workspaceGuid` 文本比较而返回 Invalid，`SavePresentation:1013-1018` 亦以原文对规范文本报 SourceChanged。本轮可信 R2 fixture 将 workspace 保持小写，因此仅证明 file GUID 迁移；初始红 fixture 的双失败可作发现线索，不能当独立完整红→绿证据。建议最小补一个大写 workspace v1 真实 Load→Save/fresh Load fixture：按已严格解析的 GUID 值比较，只在新 v2 index 写 canonical 文本，仍验证旧 UInk/旧 index 主备恢复点。第一方历史写入通常规范小写，影响是条件性的，不宣称普遍损坏。
- **组合验证边界。** 检查了相关红/绿 stderr 的 `[PresentationAutoSave] failed` 计数和现有代码/测试注册；日志与上述分批结果相符。当前原始证据为独立 `inkStrokeModelerTest.sln` Debug|ARM64 生产 Storage 模块测试，不等于完整 `InkeysRepo.sln` 最新最终 diff、真实 Controller Clear-undo 处理、Office/WPS、Win7 SP1+仅 KB2670838、Release、断电或最终可见恢复已经通过。Controller F054 同目标 transient Load 重试还在另一 owner 处理中；Storage 这三项绿灯不解除那条输入失效发布门禁。

## R4 增量独立复审：旧 v1 大写 workspace GUID（2026-09-29）

本节只读检查当前 `Draw3.PresentationAutoSave.cpp/.cppm`、`presentation_autosave_tests.cpp` 与 `ppt-f041-uppercase-workspace-r4.md`，**更新上节 R2 剩余 uppercase workspace 条件缺口的状态**，不追改其它 Finding。本 reviewer 未改产品/测试、未重新构建或启动 GUI。

| 边界 | 当前实际代码与测试 | 结论 |
| --- | --- | --- |
| v1 单条大写 workspace 旧读 | `DecodeEntry` (`PresentationAutoSave.cpp:306-342`) 仍以 `ParseUInkGuid` 验证语法并保留旧文本/路径；`LoadPresentation:1180-1188` 在严格 UInk revision/importer 之后把索引 workspace GUID 解析为值，与导入快照 GUID 比较。R4 fixture `presentation_autosave_tests.cpp:881-955` 从生产 Service 创建真正 UInk 后构造单条 v1 index，仅把 `workspaceGuid` 变为大写，旧 Load 必须读到一笔。 | **针对合法单条 uppercase workspace v1 已修复，standalone 已验证**；没有放松文件/来源校验。 |
| 续存和最后有效版本 | `SavePresentation:1012-1020` 将索引 file/workspace GUID 都解析为值后与请求比较；`:1021-1046` 仍按旧 v1 相对路径严格读取并写新的 `files/<canonicalFileGuid>_<txnGuid>.uink`，`:1051-1053` 只把待发布的新 v2 index GUID 文本规范化，`:1068-1079` 最后切 index 且不自动 GC。R4 fixture `:961-1013` 核 Committed/Base/gen21、新 index schema2/canonical 双 GUID/版本路径、旧 v1 UInk 原字节、旧 main index 成为 backup 的原字节和 fresh Service 严格读取新空页。 | **已验证本 fixture 的旧版本保留及新版本可读**。旧 main 不能声称“保存后仍是 main”；它在此次提交中成为 backup。fixture 开始时无既有 backup，不能外推到已有 backup/断电所有时序。 |
| R2/R3/三轨合同 | `uppercaseWorkspace=false` 分支仍运行大写 file GUID 的 R2 测试；R3 pending `sameLogicalFile` 继续核 `slotGeneration` (`PresentationAutoSave.cpp:640-651`)；`SelectStorageTrack` 仍按 Base/slide-id/page-index 三轨选择 (`:522-625`)，R4 没改 `.cppm` 的 request/completion 字段。完整 standalone green 测试注册包含原双轨、foreign、队列和 R1/R2/R3 用例。 | **静态无回退；原组合测试继续绿**。R4 不是新的 Controller、Office 或 Win7 验证。 |

原始日志核查：`TestResults/release-hardening/f041-uppercase-workspace-red-debug-arm64.stderr.log` 仅有新增旧 Load `Loaded` 断言 1 项失败，末尾 `1 draw3 tests failed.`；green 同名 stderr 无 `[PresentationAutoSave] failed`，stdout 末尾 `All draw3 contact input tests passed.`。两份 Build 日志均含 `inkStrokeModelerTestTests.vcxproj -> ...ARM64\Debug\inkStrokeModelerTestTests.exe`；实施记录给出 Build exit0、红 Tests exit1、绿 Tests exit0。`git diff --check` 对三文件 exit0，三文件仍为 UTF-8 BOM/CRLF。这是 standalone Debug ARM64 的真实 Storage 测试，不能推导主 Solution Release、真 Office/Win7 或断电已通过。

### R4-Pending｜P2 条件缺口，代码路径已确认，动态结果未验证

R4 报告提出的同 Service pending 比较问题在当前代码中**有明确触发链**：

1. 单条 v1 主 index 的 `workspaceGuid` 为合法大写、物理 UInk 与原 index revision 匹配；旧版本 A 可严格 Load。
2. 同 session/同 `slotGeneration` 的版本 B Save 通过 R4 值比较并写成新物理文件；若 `failIndexCommit` 或真实 index 提交失败，`:1051-1053` 已把待发布 entry 的 workspace GUID 规范成小写，`:1068-1075` 将它放入 `pendingIndexEntries`；磁盘旧 main 仍是大写 A。
3. 同一 Service、同根 `SubmitLoad` 的 selector 可继续选 Base；`LoadPresentation:1117-1128` 虽核 track/mode/source/generation，但 `iterator->second.workspaceGuid != found->workspaceGuid` 仍以大小写敏感**文本**比较，因 B 小写/A 大写而跳过该 pending。随后 `:1136-1152` 不替换 `found`，会从旧主 index 指向的 A 严格读取。上面 R4 正常提交测试没有注入这一步，不能将其写成运行已复现或已通过。

影响限定为“旧 v1 GUID 非规范大小写 + 新版本 UInk 已写成但 index 提交失败 + 同 Service/同代读取”：可能返回旧已提交 A，而不是已有的同进程自写 pending B；**最后已提交 A 的索引/UInk仍有效**，不能称为已证实的数据损坏。正常 Controller 失败 Save 会保留 dirty CPU 槽，故也不能仅凭这条静态路径断言用户笔迹实际丢失。该条件破坏现有同根 pending 自恢复合同，建议按 P2 修复。

最小直接红测：在现有 `TestUppercaseLegacyGuidCanSaveCanonicalVersion(..., true)` 的独立临时根，保留旧 A 的 main index/UInk 字节，版本 B 使用 `failIndexCommit=true` 保存并确认 `IoError/Base/gen21`、新物理文件已存在而旧 main/UInk 字节未动；同一 `PresentationAutoSaveService`/root/session/gen 提交 Current Load，必须读到 B 的明确不同笔迹/清空状态，而全新 Service 必须仍只读到 durable A。红测应以内容区分，不能只断言 `Loaded`；修复可对已严格解析的 `fileGuid/workspaceGuid` 做 GUID 值比较（同时覆盖 R2 大写 file GUID），保留 track/mode/source/generation 与严格 revision/importer 门，不把任意大小写字符串或目录孤儿当权威。随后再验证 retry Save 仍能从 pending 收敛、旧 main/UInk 不变。此建议尚未实施，不把假设的动态输出冒充红证。

## R4-Pending 修补后独立增量复审（2026-09-29）

本节**取代上节“R4-Pending 尚未动态验证”的旧状态**。只读核当前工作树、`ppt-f041-r4-pending.md` 和既有原始日志；未修改产品/测试、未重新构建/启动 GUI。当前被审文件 SHA-256：`Draw3.PresentationAutoSave.cpp` = `54dc60115592813ded66ae8f09c5e0370405e6cb83d0be62081db98c3306ada8`，`.cppm` = `c3d658003efb14e7f42191f5d69ffe44cc325791d48c454fe223af26b72eab06`，`presentation_autosave_tests.cpp` = `a07185ca8f5bbf6c4ac37dcf53e39c6c324d721311fca3c24868db03f30ae521`；三文件 UTF-8 BOM/CRLF，`git diff --check` exit0。

### R4-Pending｜P2 条件缺口：已修复，standalone 红2→绿0

- 生产 Load `PresentationAutoSave.cpp:1117-1139` 仍先按 **实际 track、binding mode、sourceIdentity、slotGeneration** 找候选；有已提交主 index 时，将 pending/main 的 file GUID 与 workspace GUID 各自 `ParseUInkGuid` 后按值比较。解析失败或任一值不同便不提升 pending；通过后若多个候选仍返回 `IoError` (`:1140-1145`)，会话/key 仍在 `:1147-1163` 核对，随后 `:1181-1195` 严格检查 UInk source revision、importer 和 workspace 身份。此修补没有扫描目录孤儿或放松三轨 selector。
- 真实 Service 夹具 `presentation_autosave_tests.cpp:881-1053` 各自隔离大写 **file** 与大写 **workspace** 两种合法 v1 index；`failIndexOnSave=true` 时 Save B 清空后返回 `IoError/Base/gen21/fileGuid`，旧 A 的主 index/UInk 字节和无 backup 状态保持，物理 UInk 数为 2 (`:961-981`)。同一 reader Service、同 root/session/gen Load 严格取得 B 的 **零笔** (`:982-995`)；全新 Service 无 pending，只读旧 durable A 的 **一笔** (`:996-1005`)。`failIndexOnSave=false` 的两个原 R2/R4 续存分支仍核 canonical v2 路径/index、旧文件与旧 main 转 backup 原字节、fresh Load 新空页 (`:1009-1051`)。测试调真实 `PresentationAutoSaveService`，没有复制 selector/Save/Load 算法。
- 原始 `TestResults/release-hardening/f041-r4-pending-red-debug-arm64.log:48,50` 仅两条新增 pending Load “应为空”断言失败，末尾 `2 draw3 tests failed.`；green 同名日志保留其它场景的预期 Save `io_error`/`source_changed` 诊断，但**无** `[PresentationAutoSave] failed`，末尾 `All draw3 contact input tests passed.`。`validation.md:147,149` 与实施记录给出 standalone `inkStrokeModelerTest.sln Debug|ARM64` Build exit0、红完整 Tests exit1、绿 exit0。测试区分 B 物理写成但 index 未发布与 A 已提交；不把同进程 pending B 说成跨进程 durable。

### 旧迁移 helper 清理与未覆盖项

`Draw3.PresentationAutoSave.cppm:45-50` 现直接进入 `ShouldReleasePresentationClearFallback`；生产与测试源码搜索不到 `ShouldPersistLoadedPresentationBindingMigration`。`git diff` 显示删的是仅供旧测试的 page-index→Stable 原文件迁移策略和三条镜像断言，保留 `PresentationStorageTrack`、request/completion generation/track/fileGuid 字段 (`.cppm:53-123`) 及真实双轨 Service 测试。实施记录和 `f041-r4-helper-cleanup-*` 显示该清理后 standalone Build/完整 Tests 均 exit0；这消除了与用户“旧文件保留，新 Stable 独立保存”相反的死 API，不是运行时性能收益。

非法 GUID 串的 **fail-closed 路径已静态核对**：磁盘 index 的 `DecodeEntry` 在 `PresentationAutoSave.cpp:328-334` 拒绝不可解析 file/workspace GUID；内存 pending 对已有主 entry 的四次解析有任一失败会跳过 pending，不会把无效串提升为权威 (`:1128-1138`)。该具体内存损坏故障没有新注入测试，故不写成动态 PASS；跳过 pending 后若旧主索引仍有效，Service 可以继续严格读取旧 A，这与“保留最后已提交版本”一致。已知 `PushCompletion` 分配失败可能丢回执、F044 版本文件长期增长、已有 backup 被后续替换及断电/目录重解析路径仍属原有门禁。

本次证据止于本机 standalone Debug ARM64。最新完整 `InkeysRepo.sln` Debug/Release、Controller 同进程 Clear/页切换、真实 Office/WPS、Win7 SP1+仅 KB2670838、15 秒强制退出以及可见像素恢复均需父任务分别验收；本补丁与 dead helper 清理不证明这些组合已通过。

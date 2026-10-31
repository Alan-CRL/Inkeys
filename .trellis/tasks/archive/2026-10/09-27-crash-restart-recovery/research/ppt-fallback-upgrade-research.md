# F-041：旧 page-index PPT UInk 到 StableSlideId 的安全边界

日期：2026-09-28。当前代码与既有测试的只读调查；未修改产品、规范或测试，未启动 Office/GUI、构建、性能采样或读取真实用户 UInk。研究对象是**已开放的当前 Inkeys 进程内 PPT 保存/恢复**；跨 Inkeys 进程自动恢复/冲突交互仍未开放。

## 当前链路与两类来源

1. `Presentation::TargetIdentityFor` (`Draw3.Presentation.cpp:150-184`) 对有规范绝对路径的文稿以路径生成 `sourceIdentity/key`，另构造 `bindingToken=provider:Office PID:show HWND:bindingRevision`；无稳定路径才把 binding 信息放进 process-local source identity。`CanUpgradePresentationBindingByOrdinal(previous,next,pageCount)` (`:368-386`) 要求同 key/source、fallback→Stable、同总页数与有效目标；**只在 processLocal=true 时**核非空 token 与 binding revision，稳定路径不核 token/session。`CanReusePresentationDocumentSlot` (`:389-422`) 沿用该宽松升级判定。
2. 当前进程内同一个活动 fallback slot 若稍后拿到完整 SlideIDs，`SetPresentationTarget` (`DrawingController.cpp` 中 `bindingUpgrade` 分支) 可把既有 `document/history` 按 ordinal 沿用到 Stable，并将此升级当持久化 mutation。这里有内存中的旧 `PresentationTarget` 可比对。即使同 token、session、页数一致，也**只能证明同一绑定标识与页数**；除非提供方或明确产品合同保证该段期间页序不变，旧 fallback 没有 SlideIDs/独立拓扑指纹，无法仅从数据数学证明每个旧 ordinal 对应新 SlideID。允许此类升级属于需明确记录前提的产品风险选择。
3. `presentation/index.json` 的 `IndexEntry` (`Draw3.PresentationAutoSave.cpp:263-328`) 保存 source/key、Inkeys `sessionId`、file/workspace GUID、bindingMode、processLocal、bindingRevision、mutation revision、slideIds 与 UInk source revision，**不保存 bindingToken 或 fallback 时的 SlideID**。旧 page-index UInk 的 Canvas 也不带 SlideID，严格导入要求这一点（`uink_draw3_import.cpp:310-360`）。因此磁盘旧文件自身没有 ordinal→SlideID 证明；同路径、同页数、同 Inkeys 进程仍可能是 Office 新放映/重排。
4. `LoadPresentation` (`Draw3.PresentationAutoSave.cpp:805-886`) 对同 `sessionId/key/source` 的旧 page-index entry 与请求 Stable target 设置 `bindingUpgrade=true`；稳定路径的 `RequiresExactFallbackBinding` 为 false，仅核页数与目标 slideIds 数量，允许 bindingRevision 变化。`MakeExpectation(entry,target)` (`:560-576`) 按**已存 entry 的旧 page-index 模式**严格读取，返回的 `loadedSnapshot` active Canvas 无 SlideID。跨 Inkeys 进程 `sessionId` 不符会返回 `CrossProcessConflictDeferred`，这条未开放的跨进程自动恢复并未被该升级放行。
5. Controller completion 的 `completion.target` 是**请求当时**的 target；归属先以 `CanReusePresentationDocumentSlot(completion.target,latestTarget,pageCount)` 判断，然后以**最新** target 调 `MaterializePresentationSlot`（`DrawingController.cpp` completion 路径）。如果请求 target 本来就是 Stable、只是磁盘旧 fallback，则没有任何 in-memory fallback→Stable 交接证据。如果请求 target 是 fallback，随后 latest 变 Stable，现有宽松 `CanReuse` 也未核稳定路径 token/session 或是否经历页序变更。F-039 materializer 对最新 Stable 下的无 SlideID 普通 Canvas fail-closed 返回空，避免旧页直接误贴；活动分支仍设 `activePresentationPersistenceInitialized=true`，但不安装旧 fileGuid/document。用户可看到空页，旧 UInk 留存。其后新笔迹尝试按新 GUID 保存时，worker 的旧 source index/fileGuid 冲突可返回 `SourceChanged`；**旧数据保留不等于新内容已有安全持久化出口**。
6. `SavePresentation` (`Draw3.PresentationAutoSave.cpp:740-790`) 也有同样宽松的 fallback→Stable `bindingUpgrade`：稳定路径不需旧 bindingRevision 相等。若收到带旧 file/workspace GUID、但 ordinal 被错误贴到新 SlideID 的请求，worker 会先按旧 fallback UInk 导入 canonical，再按请求 page GUID 合并并覆盖同一逻辑文件/索引。F-039 当前阻止这条错误请求由直接冷加载自动产生，但 worker 本身不能从请求与旧索引证明 ordinal 归属。

## 安全判断与历史意图冲突

| 场景 | 现有证据 | 可否自动按 ordinal 写 Stable 身份 |
| --- | --- | --- |
| 同 Inkeys 进程、同活动 Office show，fallback target 与下一 Stable target 有非空且相同的 bindingToken、bindingRevision、sessionRevision、页数，且有独立证据/受支持合同证明期间页序未变 | 运行时可持有两 target；本仓库目前没有冻结页序或旧拓扑指纹 | **条件性可设计**；必须把连续 binding 与页序不变当成显式前提，并在异常/间隔变化时拒绝。仅同 token 还不是页序证明。 |
| 同路径旧 page-index 磁盘文件，在同 Inkeys 进程的新 Office show 中以 Stable target 冷加载 | index 无 token/旧 SlideID；`sessionId` 只识别 Inkeys 进程 | **不可自动证明**。同页数、同路径、甚至相同 bindingRevision 都不能证明每个旧 ordinal 仍指向同一页。应保留旧 UInk 并 fail closed，不把旧墨迹静默贴到新 SlideID。 |
| 跨 Inkeys 进程旧 page-index 文件 | index sessionId 不同；当前产品跨进程恢复/冲突交互未开放 | **不适用当前自动入口**；保持拒绝，不能为测试打开产品功能。 |

现有 `InkeysHeadlessTests/presentation_descriptor_tests.cpp:142-160` 明确把“saved-path fallback 在 slideshow re-entry 时 token/revision 变化仍可 ordinal upgrade”写成旧期望；规范的同进程 fallback→Stable 描述与“严禁错页”发布底线之间存在真实取舍。安全修补不能悄悄把该旧测试改绿或当成事实证明。若要缩紧自动迁移，应先由主任务记录它对旧同路径会话体验的影响；旧文件保留与新稳定会话能否另存是两个独立验收点。

## 最小实现取舍与可执行红测

1. **先把安全门从“同路径同页数”改为可证明的迁移来源。** `CanUpgradePresentationBindingByOrdinal` 的内存槽路径至少要求非空且相同的 token、bindingRevision、sessionRevision，和同 key/source/页数；对中途有无法排除的 target revision 间隙、不同 show HWND/PID、重新绑定或拓扑事件应 fail closed。若项目不能保证同一 show 内页序不变，连这条自动按 ordinal 赋 SlideID 也不能宣称安全，应保留 fallback 身份或要求显式迁移设计。请求时 fallback、完成时 latest Stable 的场景只能用该连续证明放行；“请求本来就是 Stable、文件仍是旧 fallback”没有 live fallback 证明，一律不作自动 ordinal 映射。
2. **worker 不得靠旧索引猜测。** 直接 Stable `SubmitLoad` 命中 fallback entry 时应返回明确冲突/需迁移状态且不读取后覆盖；Stable `SubmitSave` 覆盖 fallback file 需由 Controller 发出的单次、同 binding 的受控升级证据，不能只凭 path/page count/旧 GUID。现有 index 无 token，简单比较 `bindingRevision` 只能增加约束，不能构成完整证明。不能安全完成文件升级时保留旧 UInk/index 和独立可用的新内容保存路径，或阻止用户继续画“看似成功但不可保存”的内容；该持久化产品取舍涉及索引身份/schema 与 UI，不能在本只读任务擅做。
3. **Controller 的值迁移必须保留页身份。** 若获得有效连续证明，旧 page-index Canvas 按旧 ordinal 与同次稳定 SlideID 列表成对赋值，不重建页 GUID/笔迹/EndScreen，不接受页数不等、重复/非正 SlideID、重复 page GUID 或 `latestTarget` 进一步重排/变更。F-039 的 Stable 来源投影只处理有旧 SlideID 的 snapshot；不能把“缺 ID”简单改成始终按最新 target ordinal，否则会重新引入错页风险。旧 mutation 拒绝安装门与 F-031/F-038 retained 同槽合同继续适用。
4. **无窗口红测应触及生产判定与存储。** `presentation_descriptor_tests` 用同路径/页数但不同 token、show/binding/session 与颠倒顺序的 target 先展示 `CanUpgrade...` 旧判定为 true（红），再测严格连续 case；当前旧“re-entry 允许”断言应作为历史行为变更记录。隔离 `PresentationAutoSaveService` 用两页不同 page GUID/笔迹的 page-index UInk/index：新 Stable 目标若没有 live fallback 证明，`SubmitLoad` 必须拒绝且旧文件/index 字节与 source revision 不变；`SubmitSave` 不得把旧 ordinal 的 A/B 笔迹写成交换后的 SlideID。另用同生产 `MaterializePresentationSlot` 的授权迁移入口测**同 binding、无重排** fallback 请求→Stable latest，输出与原 page GUID/点一致、EndScreen 独立，再严格导入升级文件。无 GUI 的构造不能证明真实 Office 期间没有重排；该前提必须真实提供方/设备验收。
5. **拒绝后不伪报 ready/durable。** 对当前 F-039 拒绝安装路径区分“旧文件安全保留”“当前页未恢复”“后续新编辑是否能持久化”；必要时通过现有 admission/状态门避免静默接收不可保存笔迹。不能把 `LoadPresentation` 返回 `Loaded`、completion 已处理、`activePresentationPersistenceInitialized=true` 或编译成功统称 PASS。

## 验证缺口与结论

- 只读确认旧 source/index 没有可验证的 ordinal→SlideID 映射；当前 F-039 fail-closed 避免直接旧文件覆写，但旧页不显示以及后续保存冲突是高优先级风险。磁盘失败/冲突时是否有其它产品提示或安全另存路径，当前调查未找到；主任务应继续追并作为发布门禁。
- 真 Office/WPS 的同一 show 内页序不可变性、binding token 与 revision 的实际生命周期、同路径重新放映、文件被外部改动、当前用户可见恢复与 Win7 SP1+仅 KB2670838 均未实测。用户已实测该 Win7 环境的 FLIP 可用，本报告不涉及交换链或透明模式修改。
- 本报告没有改代码、运行自动/GUI 测试或承诺已完成安全迁移。若项目选择兼容旧同路径 ordinal 复用，应在产品需求中显式接受错页风险并附真机/真实文稿证据；不能把无 SlideID 文件推断为有 SlideID 的身份源。

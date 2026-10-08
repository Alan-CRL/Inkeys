# F-048 PPT retained SlideID 与索引一致性

日期：2026-09-28。状态：未来写入路径修复完成；独立 Draw3 Debug|ARM64 编译与隔离测试红→绿；主 Solution/Release、独立最终 diff review、Win7/Office 真机与既有受影响文件恢复未验证。未提交 Git。

## 根因与修改边界

F-044 最终独立审查在旧 `SavePresentation` 路径确认：首次 index 创建或只有同根 pending entry 时，`entry.slideIds` 只取当前 `target.slideIds`；已经通过 `ValidatePresentationSaveRequest` 的 retained 页可与当前页一起写入 UInk，却不进入索引 known set。fresh Service 用该索引构造严格 `MakeExpectation`，对 UInk 内缺席于 known set 的 retained SlideID 返回 `TopologyMismatch`，最终 `LoadPresentation` 为 `Invalid`。既有 found 分支只合并旧 known 与当前 target，若本次写出的 retained ID 尚未出现在旧索引，也有相同风险。这是 H0 起点以前的漏洞，不是 F-044 版本化文件引入。

本次只改 `Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cpp` 的 `SavePresentation::buildDocument` 与三条调用路径，以及 `inkStrokeModelerTestTests/presentation_autosave_tests.cpp`；没有改 UInk importer、Controller、Host、Bridge、PPT COM 或 `.cppm` API。`buildDocument` 在 `MergePresentationSnapshot` 得到本次**实际将导出的** active/retained 画布后，组合旧可信 known IDs、当前 target SlideIDs 和这些画布的 SlideID。EndScreen 没有 SlideID，不进入集合；同一输出内重复或非正 SlideID、超过现有索引最大 known 数量都在写新 UInk 前返回 Invalid。准备好的 UInk document 与 known IDs 同属一次局部结果，首次、pending 重试、既有 entry 保存都把该结果用于本次物理版本和索引。保留旧已知 ID，供已删除页未来重现；不以目录扫描的孤儿猜测来源，也不放宽严格 importer。

## 真实生产 Service 红→绿

1. `TestFirstRetainedCommitKeepsKnownSlideAndEndScreen`：Stable target 当前真实页 101，第一次全量快照还带 retained 页 202 与独立 EndScreen。先要求主 index known `{101,202}`，再销毁旧 Service，用全新 Service 严格 Load，核 101、retained 202 的 pageGuid/墨迹及 EndScreen pageGuid/墨迹不串。绿码继续第二次保存，要求主备两份索引都保留 known 集，并损坏主索引使 fresh Service 严格从 backup 恢复同一页身份。
2. `TestPendingFirstCommitRetainsDeletedSlideOnRetry`：首次 target `{101,202}` 的 UInk 成功但 index 注入失败，形成同根 pending；拓扑删除 202 后，target 变 `{101}` 且快照把原 202 以原 pageGuid 放入 retained。重试成功后要求主 index known `{101,202}`；销毁旧 Service，fresh Service 严格 Load 保留 202 的身份/墨迹。旧 pending 被读取时仍按旧可信 `{101,202}` 进行 importer 校验，不把失败孤儿当新权威。

红测先于修复冻结并由主 agent 串行执行：`inkStrokeModelerTest.sln Debug|ARM64` Build exit 0、沙箱外只用隔离项目数据的完整 `inkStrokeModelerTestTests.exe` exit 1，四个新断言失败：两场景的 index known 集缺 202，fresh strict Load 为 `Invalid`。日志 `TestResults/release-hardening/f048-retained-index-red-build-debug-arm64.log`、`f048-retained-index-red-debug-arm64.{stdout,stderr}.log`。生产修补后同一 Solution Build exit 0、完整测试 exit 0；`f048-retained-index-green-build-debug-arm64.log`、`f048-retained-index-green-debug-arm64.{stdout,stderr}.log`，stdout 末尾 `All draw3 contact input tests passed.`，无 `[PresentationAutoSave] failed`。测试目标编译并执行真实生产 PresentationAutoSave/UInk 模块，非复制保存算法的替身。

`git diff --check` exit 0；两个目标文件保持 UTF-8 BOM 与 CRLF。性能方面只在低频 PPT 保存 worker 上扫描本次画布与少量 known ID，未测真实多页文稿保存尾延迟，不能报量化提升。

## 剩余发布边界

- 本修补防止今后写出新不一致组合；若旧 Canary/H0 已提交过 `index known{101}` 但 UInk 内 retained202，严格 Load 与续存仍会失败。本次不自动从文件扫描未知 SlideID 并信任它改写索引，否则会弱化源身份边界。主任务需调查历史受影响文件是否可达用户、决定受控恢复或人工提取策略，不能把未来写入绿测当作历史损坏已修复。
- F-041 用户决定“保留旧 page-index 文件，新 Stable 会话独立保存”仍是单独 P1；当前 ordinal 升级路径和存储 sidecar/Controller 双 lane 尚未实施。F-048 没有更改该语义，不能据本轮 PASS 关闭 F-041。未来 sidecar 写入必须沿用这里的实际输出 known SlideID 合同。
- 已知集合上限当前与 `Bridge::kMaximumPresentationPages=10000` 一致；若单文稿长期累计超过 10000 个曾出现过的独立 SlideID，本次会在写 UInk 前拒绝保存并保留旧恢复点。真实 PPT 工作负载、磁盘满/断电、Win7 SP1+仅 KB2670838、Win32/x64/ARM64 Release、Office/WPS 与跨进程可见恢复均未由本单元验证。用户实测的 Win7 `FLIP_SEQUENTIAL` 和只选 DComp/ULW 图形约束未受影响。

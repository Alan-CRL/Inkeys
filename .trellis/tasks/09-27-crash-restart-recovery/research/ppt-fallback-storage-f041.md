# F-041 PPT page-index / StableSlideId 独立存储轨

日期：2026-09-28。状态：Storage 实现和独立 Draw3 Debug|ARM64 隔离测试已通过；Controller 双 lane、最终主 Solution Debug/Release、真实 Office/Win7 与独立全 diff 审查仍待集成。未提交 Git。本报告只描述 `Draw3.PresentationAutoSave.cpp/.cppm`、`inkStrokeModelerTestTests/presentation_autosave_tests.cpp`，不把 Controller 策略门绿测冒充整链完成。

## 用户决定与共享接口

用户已选择“旧 page-index PPT 文件保留，新 StableSlideId 会话独立保存”。Storage 不再从旧 fallback UInk 按 ordinal 自动导入 Stable。共同字段合同先写入 `ppt-f041-storage-controller-contract.md` 并获主 agent 确认：`PresentationStorageTrack` 分 `Unresolved/Base/PageIndexSidecar/SlideIdSidecar`，表示实际物理 index 根；逻辑 lane 始终以 `target.bindingMode` 表示。Save/Load Request 的末尾 `slotGeneration` 是 Controller 不透明代次，所有已产生的 worker completion 原样 echo。Completion 的 `fileGuid` 在 Save 回显请求 snapshot GUID（包括失败/异常），在 Load 仅 `Loaded` 且 UInk source revision、严格 importer、workspace 身份均核准后提供实际 GUID；选择前失败 track 为 `Unresolved`，选定后回显实际轨。旧聚合初始化字段顺序保留，新增字段均放末尾。

Controller 必须以 `(PresentationKey,bindingMode,slotGeneration)` 路由旧回执，Save 再比 fileGuid/历史 revision，PreviousInterval 还比 pageGuid/ordinal；Storage 不从 generation 生成文件名或信任文件内容。灾难性 completion 分配失败时现有 worker 只能记录错误而非保证事件送达，此边界不应伪称“绝对每件请求有回执”。

## 实际实现

- `SelectStorageTrack` 在**同一个规范化 `<AutoSave>/presentation` named mutex** 内严格读取 base、`presentation/slide-id/`、`presentation/page-index/` 三处主/备 index。已存在的同 mode entry 优先继续原根（含历史 Stable Base）；base 已有相反 mode 时，当前 mode 选固定 sidecar。base 旧 fallback 下 Stable sidecar 无 entry 的 Load 为 `NotFound/SlideIdSidecar`，不读旧页序。两处同 source+mode、source/key 不符、任一 index 主备均无效及 pending/committed mode 矛盾时 fail closed，不猜权威版本。目录名是常量，不拼 Office 路径。
- 新轨 Save 先核请求 file/workspace GUID 与另一轨已提交 entry、当前同根 pending 不冲突；碰旧 GUID 返回 `SourceChanged`，旧 base 文件/索引字节不变。选轨后只在该轨 `files/` 用 F-044 `CreateNewLogicalFileWithIdentity` 写新物理版本，再在同轨用 schema v2 索引最后切引用；各轨自有 `index.json(.bak)`、self-written pending 与 source revision。没有恢复自动版本 GC，旧页序文件与未知孤儿都不删除。
- Tail latest-wins 仅当 key/source/mode/fileGuid/workspaceGuid/slotGeneration 均相同且未跨同 key 的 Clear boundary 才替换。跨 mode 已接受请求各自产生终态；Clear 始终 FIFO。pending 键含实际 track、mode、source、file/workspace GUID 和 generation，防止 sidecar index 失败后误修 base。Load 只在同轨、同代且来源唯一时使用严格验证的 pending；全新 Service 无 pending 仍按索引返回 NotFound。Stop/Start 同 AutoSave root 的 pending 仍沿用既有保留边界，切 root 清除。
- F-048 的 known SlideID 合同适用于 Base 与 sidecar 两轨：索引来自实际合并写出的 active/retained 加旧已知集合，EndScreen 不生成 SlideID；成员判重已改为集合查找，静态复杂度由本轮新增 O(P²) 收敛为 O(P logP)。这不是全链保存尾延迟的实测结论。

## 红→绿证据

先在真实 `PresentationAutoSaveService` 的独立测试加入两页 fallback `{ordinal0,ordinal1}` 与反序 Stable `{SlideID 202,101}` 同 key/source：旧版 Stable Load 错误返回 Loaded，同旧 GUID Save 改写 base，独立新 GUID 无 sidecar，fresh 两轨 Load 身份不匹配。`inkStrokeModelerTest.sln Debug|ARM64` Build exit 0，完整 `inkStrokeModelerTestTests.exe` exit 1、8 项预期新断言失败；日志 `TestResults/release-hardening/f041-storage-red-build-debug-arm64.log`、`f041-storage-red-debug-arm64.{stdout,stderr}.log`。

实施后的三轮均由主 agent 串行构建/运行，构建和全套测试均 exit 0：Stage2 主双轨红8→绿0（`f041-storage-stage2-build-debug-arm64.log`、`f041-storage-stage2-debug-arm64.{stdout,stderr}.log`）；Stage3 补真实 worker event 暂停后的跨轨队列不得互替、sidecar index 失败/同根 pending/fresh NotFound/原轨保存/重试、foreign 两轨拒绝、Save/Load worker 异常回执代次和文件身份（`f041-storage-stage3-*`）；Stage4 补历史 Stable Base 优先、反向新 fallback 进 PageIndexSidecar、同 mode 双根索引 fail-closed（`f041-storage-stage4-*`）。所有测试直接编入同一生产 PresentationAutoSave/UInk 模块的 standalone 测试 EXE，不复制保存算法。红灯和绿灯日志里的设计内 `io_error` 注入与整进程退出码分开判断。

原 `TestPageIndexFallbackOverwrite` 的“同路径按 ordinal 原位升级”断言按用户新决定改为 sidecar NotFound、独立 GUID Save 和旧 base 字节保留；process-local 相同 fallback mode 的 binding revision 冲突仍保留。`git diff --check` exit 0，目标 `.cpp/.cppm` 与测试文件原 UTF-8 BOM/CRLF 全保持。

## 当前缺口与发布门禁

- Controller 仍需同 key 的 fallback/Stable CPU document/history/retained/fileGuid 分 lane，旧已接受请求的 retiring slot 保留到终态，并在 Save/Load/PreviousInterval 回执上核 mode+generation+GUID。Storage 独立绿灯不能保证新 Stable UI 画面不显示旧 fallback 点或退出时双轨都保存；主 Solution 当前代码需在 Controller 集成后重新 Debug/Release 验证。
- 读写每次需检查固定三轨的 index；任一相关 index 结构失效即 fail closed。这保护“不猜最新”但可能使其它轨的操作也暂时不可用，且增加低频 PPT worker 的 JSON/磁盘成本；未测真实多文稿尾延迟。新 v2 sidecar 旧二进制不保证降级读回；用户历史数据不可删除。
- Sidecar UInk 写入失败、`ReplaceFileW` 部分失败/断电、磁盘满/低空间、Win7 SP1+仅 KB2670838、Win32/x64/ARM64 Release、Office/WPS、真实 15 秒强制退出、跨进程可见恢复均未由本单元测试。F-044 禁 GC 导致的无界版本磁盘增长仍是 P2；F-048 前已提交的不匹配 retained known set 不自动修复。图形代码未动，Win7 实测 `FLIP_SEQUENTIAL` 与仅 DComp/ULW 的约束保持。

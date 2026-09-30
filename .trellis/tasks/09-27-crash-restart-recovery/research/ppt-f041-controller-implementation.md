# F-041 Controller 实施记录

日期：2026-09-28。Owner：Controller 子任务。本记录只对应 `Draw3.Presentation.cpp`、`Draw3.DrawingController.cpp/.cppm`；Storage Service 与 CLI/工程入口由其他 owner 维护。

## 改动边界

现状：同源同 key 的 page-index fallback→StableSlideId 已禁止 ordinal 迁页，但 `SetPresentationTarget` 仍将这次合法跨模式切换归为 topology conflict，进入没有保存身份的 isolated 槽。`presentationSlots` 仍只按 key 存一个停放槽；异步完成仅用 key/宽松 target 复用判断，无法区分旧 fallback 与新 Stable 槽代次。

目标：`(PresentationKey,bindingMode)` 各有独立 CPU 文档和历史，候选 Stable 的完整 `N+1` 页与新 GUID 分配成功后才换活动槽。旧 fallback dirty/pending 仍可接收其原代次回执；不从旧文件按 ordinal 恢复。Current Load 的 NotFound 才能准入新空槽；IoError、SourceChanged、foreign 保持未就绪。退出/fatal 扫双 lane。已有同 mode SlideID 重排和 EndScreen 边界保留。

不触及：Storage 物理轨选择/索引 schema、Host/RTS/窗口或 GUI 流程；不新开产品恢复入口。

## 阶段证据

- Stage R：将 `Run` 的 CPU 槽 swap/blank/切换逻辑提取为共用生产 helper；新增无 HWND `RunFallbackStableControllerLaneProbe()`，由主 agent 唯一接 CLI `--draw3-fallback-stable-lane-test` 并串行构建/运行。探针构造旧 fallback 三页含墨迹及已接受未提交保存，要求同源 Stable 反序 `{202,101}` 独立槽/身份。完整 Debug|ARM64 Solution Build exit 0，隐藏 CLI pid 45520 exit 1，三项预期失败：进入 isolated、无 Stable 目标身份、无独立 N+1 页与新 GUID。日志见主任务 `update-http-f041-lane-red-build-debug-arm64.log`、`f041-lane-red-explicit.{stdout,stderr}.log`。
- Stage G 候选：`PresentationParkedSlots` 改 `(key,mode)` 键；新文档候选和 target 拷贝成功后才交换活动槽，并分配非零 generation。Save/Load request 带 generation；completion 先经同 lane/代次/目标合同路由，Save 再核 fileGuid/revision。Current Load 只接受严格 Loaded 或已选根 NotFound；错误/foreign 留未就绪，PreviousInterval 另核页 GUID/ordinal/文件 GUID。探针覆盖旧迟到回执路由、双向切换、Stable 反序及 EndScreen、候选分配失败。主 agent 的完整 Debug|ARM64 Solution Build exit 0；显式隐藏 CLI pid 64948 exit 0，`InkeysHeadlessTests --no-window` exit 0/PASS，日志 `f041-controller-update-http-final-build-debug-arm64.log`、`f041-lane-green-explicit.*`、`f041-update-http-final-headless.*`。此绿码尚不等于磁盘双轨或 Office GUI 验收。
- Stage G 自审加固：探针新增从旧停放 fallback 槽通过生产 `BuildPresentationSaveRequest` 导出旧页墨迹/workspace/page GUID、fallback workspaceType 与 generation，再对照新 Stable request；仅测试 fixture 建全页 canvas，生产逻辑未改。Storage owner 指出物理 track 需与 logical mode 一致，现用 `PresentationTrackMatchesMode` 限定：Base 可承载两模式，Stable 只允许 SlideIdSidecar，fallback 只允许 PageIndexSidecar；Current NotFound、Loaded、Save Committed 分别核验。新增错误 sidecar NotFound 拒绝测试。主 agent 再次完整 `InkeysRepo.sln Debug|ARM64` Build exit 0，显式隐藏 `--draw3-fallback-stable-lane-test` pid 35820 exit 0，`InkeysHeadlessTests --no-window` exit 0/PASS；日志 `f053-json-f041-lane-final-build-debug-arm64.log`、`f041-lane-final-explicit.*`、`f053-f041-final-headless.*`。本 Controller 候选已冻结，交独立最终 diff review。

## 当前实现与所有权

| 位置/符号 | 行为变化 |
| --- | --- |
| `DrawingController.cpp::PresentationLaneKey`、`presentationSlots` | 停放文档由 `(PresentationKey,bindingMode)` 唯一定位，旧 fallback 和新 Stable 同源共存。活动文档仍只是一份 CPU/renderer/线程，不增加 Host。 |
| `TryCreateBlankDocumentSlot`、`SwapActiveDocumentSlot`、`SwitchPresentationCpuSlot` | 候选 `N+1` 页面、独立 workspace/page GUID、target 复制和 generation 均在活动槽交换前准备；分配失败保持旧活动文档。旧 mode dirty/pending 保存状态随整槽停放；同模式复用与 Stable SlideID 拓扑重排仍走原合同。 |
| `BuildPresentationSaveRequest`、`submitPresentationSlot` 与 Current/PreviousInterval Load 发布点 | Controller 的非零 `slotGeneration` 随每个已接受请求传到 Storage；旧请求不会被当成新槽的完成。首次 Stable 的 fileGUID 与旧 fallback 不共享。 |
| `RoutePresentationCompletion`、`PresentationSaveCompletionMatchesSlot`、`PresentationLoadedForLane`、`PresentationEmptyLaneVerified` | 迟到完成先核 `(key,mode,generation)`，Save 另核 fileGUID/revision；PreviousInterval 另核 fileGUID/pageGUID/ordinal；Current Load 仅严格 Loaded 或正确物理根的 NotFound 可发布 ready。IoError、SourceChanged、foreign、错误根均保留未就绪与重试入口，不将空白假装为恢复。 |
| `captureExitAutoSave`、`SetWorkspace` | 活动与停放双 mode lane 都按原 owner 的 worker 请求捕获；停放/淘汰用对应 mode 键。可淘汰 CPU 槽不意味着删除旧索引或 UInk，物理保留由 Storage F-041/F-044 合同保障。 |

`Draw3.Presentation.cpp` 未改：该文件之前已按用户决策禁止 `CanUpgradePresentationBindingByOrdinal`，本轮 Controller 实施只在上述 owned Controller 源和导出接口中修改。`Draw3.PresentationAutoSave.cpp/.cppm`、standalone 测试、`IdtMain` CLI 与规范归其各自 owner；本 worker 没有修改它们。

## 可观察与剩余风险

- 本探针复用了产品 CPU 切换、快照构造与 completion 路由，但不创建真实 Drawpad HWND、不经过 Office COM、RTS、GPU 成功 Present、真实磁盘索引。它不能证明 PPT 页码 UI ready、物理输入门、实际 save/load 或 Win7 组合已通过。联合 Storage Service 原文件测试与真实 Office 多文稿/结束页/跨进程重启仍由主任务分别记录。
- 同 mode 复用策略仍沿用现有 `CanReusePresentationDocumentSlot`；本次只隔离 fallback 与 Stable，不强制每一场相同 Stable 放映都新建文件。
- 拒绝 Current Load 后，槽保持未就绪，现有同 target 后续请求可以再试；未引入新的定时重试策略。磁盘持续失败时需观测是否高频重复请求，不能将此情况算作产品可用。
- `ShouldEvictPresentationSlot` 只淘汰已 durable 且干净的 CPU 槽。大量从未落墨、NotFound 的不同文稿可长期保留空白停放槽；本次未改淘汰策略，因为其跨会话身份/可见成本需单独测量。
- 旧 fallback 和新 Stable 的实际双轨文件隔离依赖 Storage Service 严格 `storageTrack` 选择、同根 named mutex 与 F-044 物理版本事务；Controller 不直接写盘。不能以本探针单独宣称 durable 恢复通过。

## 待验证

- 已验证：红测精确落在已知 isolated 行为且完整 Debug Solution 编译通过；双 lane CPU 成组切换、generation/fileGuid/Load 状态路由、迟到 Save/Load/PreviousInterval 的无 HWND 生产 helper probe 绿；最终加固后完整 Debug Solution、Headless 绿。
- 待主任务：Release Solution、Storage Service 与 Controller 的联合磁盘/内存链、F-029/F-031/F-038/F-039 后续重跑、独立 diff review。
- 真实 Office、Win7 SP1+仅 KB2670838 运行与可见呈现需由主任务验收，不能由本无窗口探针推断。

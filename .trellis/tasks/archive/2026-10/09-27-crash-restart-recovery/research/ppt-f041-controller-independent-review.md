# F-041 Controller 双轨最终 diff 独立复审

日期：2026-09-28。范围：只读检查 `Draw3.DrawingController.cpp/.cppm` 的当前实际 diff，沿 `Draw3.Presentation.cpp`、`Draw3.Host.cpp` 与 Storage Request/Completion 合同追调用链；参考 `ppt-f041-storage-controller-contract.md`、`ppt-f041-controller-implementation.md` 及 F-029/F-031/F-038/F-039/F-045/F-048 的已记录边界。此 reviewer 仅参与过早期 ordinal 策略门/Stage1 probe，没有实施本次 lane、generation、回执业务。没有修改产品/测试/父任务账本，也没有启动构建、GUI 或性能采样。

## 必须处理的发现

### F041-C1 / F054｜P1｜已确认条件缺陷：瞬态 Current Load 失败后，输入可持续失效

- 新 Controller 在 Current Load `IoError`、`SourceChanged`、`CrossProcessConflictDeferred` 或严格校验不通过时，`DrawingController.cpp:7425-7431` 清 `activePresentationLoadPending` 并保持 `activePresentationPersistenceInitialized=false`。这比 H0 把失败空槽误标已恢复安全，但 `3515-3519` 使 `presentationLoadUnresolved()` 持续为真；`5408-5414` 会丢弃每次 physical Down 到终态，`7605-7611` 也抑制破坏性画布命令，且不会调用 `stageWorkspaceReady`。
- 唯一同目标的重新 SubmitLoad 在 `DrawingController.cpp:7484-7500` 的 `SetPresentationTarget` 命令中。Host `Draw3.Host.cpp:900-911` 仅 targetRevision 变化或 `restoreLatestScene` 才排入该命令；`:947-962` 的命令场景也需要外部新命令；Canvas/worker 完成自身的 wake（`Host.cpp:1018-1026`）只泵 completion/state，不会凭原 revision 产生重复目标命令。因此一次性 I/O 失败恢复后，若 Office/用户没有再发新目标，可无限保持空画布且输入被拒。现有 lane probe 只调用 `PresentationEmptyLaneVerified` 判断错误状态不可 ready（`DrawingController.cpp:2553-2575`），没有走生产失败→自动再试→成功的 `Run` 轨迹。
- 状态边界：对 **IoError**（例如暂时读失败）应在同代、同 key/mode/target 下安排有界退避的重新读取，复用 `input_.WaitForWake` 的有限 timeout/已有 control wake，并阻止并发重复 Submit；永久失败不能高速空转。**SourceChanged** 意味着文件身份/版本不再可信，**foreign** 表示其它会话持有该 entry；二者不能把失败空槽解禁后保存，也不宜无证据无限自动重试，应保留 fail-closed，并提供明确可观察状态与安全的用户/目标身份变化后的重试路径。新状态到来前旧文件不可被空槽覆盖。
- 最小直接生产回归：在独立 no-HWND `Run` 可共用入口中，让同 targetRevision 的真实 Storage/observer 首次 Current Load 返回一次 `IoError`、随后返回经严格核准的 `Loaded` 或所选轨 `NotFound`；旧行为须红，修正后断言重新提交次数有限、同 generation/track、最终 ready/input gate 开放，且失败阶段零 Save/无旧 UInk 覆盖。分别断言 `SourceChanged/foreign` 不自动宣称 ready、不进入高频轮询，且能从明确的安全目标变化/重试入口恢复。修后需串行主 Solution、Headless、lane CLI 复验。

### F041-C2｜跨层 P1，Storage owner 已获通知：PreviousInterval 异常回执被误识别为 Current

独立 Storage 复审 `ppt-f041-storage-independent-review.md` 的 R1 已给出代码证据：Storage worker 在 Load 抛异常时未预填 `loadKind/pageGuid/intervalOrdinal`，Controller `DrawingController.cpp:7327-7412` 因默认 `Current` 跳过 interval 清理，转而在 `7425-7431` 处理整页失败，`runtime.intervalLoadPending` 可长期不清。此项修复所有权在 Storage；Controller 联合回归仍应证明错误回执解除该页 pending，防止只测 Storage 字段。

## 合同与实际代码检查

| 关注点 | 证据与结论 | 状态 |
|---|---|---|
| `(key,mode)` lane 与有序换槽 | `PresentationLaneKey`/`PresentationParkedSlots`（`421-435`）将旧 fallback 与新 Stable 分槽；`SwitchPresentationCpuSlot`（`514-609`）先复制目标、建 `N+1` 完整候选页/独立 workspace 与 page GUID、分配非零 generation，再通过 `SwapActiveDocumentSlot` 成组交换 document/runtime/retained/fileGUID/revisions/load 状态。候选分配失败早退不写活动槽。旧 mode dirty/queued/completion 均随槽停放。 | 静态已验证；no-HWND 生产 helper 已验证 |
| 同 mode 门禁 | Stable 旧/新 target 由 `CanReusePresentationDocumentSlot` 与 `StablePresentationTopologyChanged` 继续按 SlideID 重排；fallback binding revision/页数变更仍进既有隔离路径。跨 mode 不再调用 ordinal upgrade，历史旧文件不直接继承。`RebindStablePresentationTopology` 的旧失败时序未由本 F041 probe 做故障注入，仍需沿既有 F-039 回归核对。 | 静态核对；失败/真 Office 未验证 |
| 新文件身份 | 新 Stable 候选 `fileGuid` 初始为空；首次生产 `BuildPresentationSaveRequest`（`935-1038`）才创建 file GUID，且不等于停放 fallback 的旧 GUID。workspace/page GUID 在换槽前已独立。`slotGeneration` 对新 lane 非零，生产 Save、Current Load/retry、PreviousInterval Load 分别在 `6543-6549,7494-7499,7554-7559,7730-7739` 传给 Storage。 | 静态与 lane CLI 已验证；真实磁盘联合未验证 |
| 旧 fallback 保存/retained/EndScreen | `captureExitAutoSave`（`6606-6627`）遍历活动与所有 parked lane，使用各槽 target/document/revision/generation/retained；`RetainedSlidesForSave`（`689-695`）不给 parked 使用活动 retained。F041 probe `2420-2594` 将旧 fallback 笔迹/page GUID/私有 workspaceType 导出，再核新 Stable 反序 SlideID 与独立 EndScreen 页。fatal presenter/history 退出也调用同一捕获入口（`7951-7979`）。 | 静态与无窗口 probe 已验证；durable/强制退出未验证 |
| 迟到 Save 回执 | `RoutePresentationCompletion`（`619-644`）先按活动或 parked 的 key/mode/generation/source/reuse 门路由；`PresentationSaveCompletionMatchesSlot`（`647-655`）再核 file GUID/非零 revision/≤queued。只有 Committed 且物理 track 与 mode 匹配才推进 committedRevision/release Clear fallback（`7265-7323`）。旧 lane 完成不会提交到新 Stable。 | 静态已验证；直接 probe 覆盖旧 Save 和错误 GUID |
| Current/PreviousInterval Load | Loaded 必须有严格 Storage snapshot、file GUID、mode 对应 workspaceType 和合法 Base/sidecar track；NotFound 仅正确 track 可准入空槽（`658-687,7414-7476`）。PreviousInterval 路由后还核文件、页 GUID、ordinal 与 runtime pending（`7327-7411`）；F041-C2 是异常字段丢失的跨层例外。 | 普通路径静态/无窗口 helper已核；异常缺口确认失败 |
| 淘汰与持久化 | `ShouldEvictPresentationSlot` 只在已 committed/干净/非 load pending 的条件下清 CPU parked 槽；Storage 物理索引/UInk 不由 Controller 删除。大量不同 key 的空白或未落盘 parked 槽可持续占 CPU 内存；F041 增加每 key 双 mode 共存的上界，暂无长场景资源采样，不能叫内存泄漏或性能通过。 | 策略静态已核，趋势未验证 |

## 原始验证证据核对

- `TestResults/release-hardening/f041-lane-red-explicit.stderr.log` 有旧代码进入 isolated 的一条诊断和三条指定 FAIL（cross-mode 未进持久新 lane、新 Stable 无目标身份、无独立 `N+1` 页面/工作区）；实施者/主 agent 的显式隐藏 CLI 进程 pid45520 exit1，红测对应生产共用 helper。
- `f041-lane-green-explicit.stderr.log`、`f041-lane-final-explicit.stderr.log` 均输出 `[Draw3PptLane] PASS`；父任务记录显式进程 pid64948、pid35820 exit0。相关 `f041-controller-update-http-final-build-debug-arm64.log`、`f053-json-f041-lane-final-build-debug-arm64.log` 都有完整 Debug|ARM64 `Inkeys.vcxproj`、`PptCOM.Tests`、`InkeysHeadlessTests` 产物；父任务记录主 Solution Build 与 `InkeysHeadlessTests --no-window` exit0。`git diff --check` 对这两个 Controller 文件无报错。这些证据仍只覆盖 ARM64 Debug 与无 HWND 路径；未实测 Office、用户输入、可见 Present、Win7 SP1+KB2670838 或 Release。
- 当前 `.trellis/spec/native-desktop/draw3-integration.md` 仍写旧 `map<PresentationKey, DocumentSlot>`、单 Base `index.json/files/<fileGuid>.uink` 合同，已与 F041 的 `(key,mode)` 和 sidecar/v2 版本化物理路径不符。父任务应按冻结实现及用户“旧文件保留，新 Stable 独立”决定更新适用规范，不能把旧文档当成产品必须回退的依据。

## 最小交接

先修 F041-C1/F054 和 Storage F041-C2，保持失败时不开放空槽输入/Save；对相关失败轨迹取可重复红→绿。随后将当前 Controller/Storage 最终 diff 一起独立复查，串行重跑完整主 Solution/Headless/生产 CLI、standalone Storage 与 F-029/F-031/F-038/F-039/F-045/F-048 受影响测试。Office、Win7、Release 与主观落笔仍按父任务门禁保留为人工或未验证。

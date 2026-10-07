# F-029 parked Desktop Exit 独立复审

基线：H0 `8b156fca59f0337a6afc6d722941666fcf143080` 与当前工作树。范围为 `Draw3.DrawingController.cpp/.cppm` 的 Desktop Exit 源选择、快照及无 HWND 测试，以及 `IdtMain.cpp` 的显式 CLI 分派；其他并行 Draw3/启动 diff 不归入 F-029。只读源码和既有日志；本 reviewer 未构建、运行 CLI、启动 GUI 或采样性能。

## 结论与问题分级

| 等级 | 结论 | 证据 / 最小后续动作 |
| --- | --- | --- |
| 新 P1/P2 | 未确认 F-029 补丁引入的新严重缺陷 | `SelectDesktopAutoSaveSource` 在 active Desktop 选 active；只有 Exit 且当前为 Presentation/Whiteboard 时才选 parked Desktop。生产 lambda 将 active 的 `document_/pageRuntimeStates/currentPageIndex_` 或 parked 的 `desktopSlot.document/pageRuntimeStates/currentPageIndex` 成组传入同一 helper。见 `Draw3.DrawingController.cpp:525-560,2120-2131`。 |
| P2，既有状态/诊断缺口 | Exit barrier 的 prepared 回执不证明 eligible Desktop 快照已入队或 durable | Helper 在 `CreateUInkGuid` 失败、page/canvas 不存在或 stroke kind 不支持时可静默 `false`；`PrepareExitAutoSave` 忽略该返回值并 `reportCommand`，Host observer 随即置 `exitAutoSavePrepared=true`，`Host::Stop` 等到回执后仅 drain 已接受的请求（Controller `:572-622,6405-6420`；Host `:427-432,1321-1344`）。callback `Submit` 拒绝会由 Host 记 `reason=closed/invalid`，异常也有日志；这些静默分支和 prepared/queued 区分在 H0 active Desktop 旧实现中同样存在，不是本补丁新增。最小后续是给 eligible capture 明确失败 reason，并把 `NotEligible/Queued/Failed` 状态传到退出记录；仍应发屏障回执，避免退出等待卡死。不能把 prepared 当成文件写入成功。 |
| P2，独立 F-026 | 图形致命退出先销毁 Controller 的路径仍绕过最终屏障 | 本补丁只覆盖正常 `PrepareExitAutoSave`。`DrawingController::Run` 的恢复失败直接 `RequestExit`、Host 销毁 controller 后，晚到 `Host::Stop` 不能补做 parked/active 快照；详见 `draw3-fatal-exit-design.md`。不能用本次正常退出 green 宣称故障退出无损。 |

## 生产源与事务链

1. H0 的 `captureDesktopAutoSave(Exit)` 在 `activeWorkspace != Desktop` 时因 `ShouldCapture(activeWorkspace, ...)` 返回 false，即使 `desktopSlot` 保存了未 Clear 的 Desktop 文档。当前 `CaptureDesktopAutoSaveForScene` 先按 workspace/trigger 选 Desktop 源，再将 `Bridge::Workspace::Desktop` 传给旧 policy；保存开关关闭、当前选中 Desktop history 没有可见 item、缺 callback/source/runtime 时均不构造快照、不提交请求（`Draw3.DrawingController.cpp:532-561`；`Draw3.AutoSave.cpp:703-708`）。这使 parked Desktop 仅在 Exit 被捕获，不让非 Desktop Clear 借用 Desktop 开关。
2. 切到 PPT 时 `parkedActiveSlot()` 按离开侧 workspace 返回 `desktopSlot`，`swapActiveDocument` 同步交换文档、page runtime 和 page index；Whiteboard 同样使用独立槽。返回 Desktop 时再把 `desktopSlot` 整组换回 active（`Draw3.DrawingController.cpp:4970-5004,6195-6245,6310-6335`）。因此生产传入的 parked source 不是当前 PPT/Whiteboard 文档。源 struct 本身不带 workspace tag，正确性依赖这条 slot 所有权链；无 HWND 测试没有执行真实 swap，真实 Office/Whiteboard 仍需动态门禁。
3. Helper 只读取选中源的 `WorkspaceGuid`、`PageAt(source.pageIndex)` 的 `PageGuid`/viewport/Canvas strokes，以及同一 `source.pageRuntimeStates[source.pageIndex].history` 的 visible items。逐项用 history 的 `strokeIndex` 在**同一个 canvas**取笔迹，越界返回 false；输出单页 UInk 仍用原先 `pageIndex=0/pageNumber=1` 的 interval schema 与相同 observer/worker（`:565-628`）。不能把当前 PPT/Whiteboard 页 GUID 或笔迹混进 Desktop 快照。测试以不同 workspace/page 标记和笔迹首点辨认来源。
4. `captureDesktopAutoSave` lambda 只在 Clear 和 Exit 调用（`:6381-6386,6405-6409`）。Clear 仍先按当前页产生 `preClear`，只有 active Desktop 且有旧可见笔迹才调用 `Clear` 捕获；`SelectDesktopAutoSaveSource` 对 active Desktop 返回 active，所以旧 Clear 的保存/内存恢复点与 `CompleteDesktopClear` 语义未改变（`:6340-6401`）。工作区切换本身只调用 Presentation 捕获和 slot swap，没有新增 Desktop 同步 UInk 复制/写盘。新增 Desktop 快照复制成本只在符合资格的 Exit 出现；实际大画布耗时/内存尚未量化。
5. `PrepareExitAutoSave` 允许穿过 Presentation load-pending 输入门，随后捕获 Desktop、当前和 parked Presentation，再报告命令回执；Host `StopWithFinalCommand` 先关命令生产、停 RTS，等待回执并 `CloseAndDrain` 已接受的请求（Controller `:6339-6348,6405-6420`；`Draw3.Bridge.h::PresentationCanvasCommandSuppressed`；Host `:1317-1344`）。这证明 queued 请求有排空顺序，未证明未入队的 eligible capture 失败会阻止退出，也未执行真实文件/index 回读。

## 测试真实性与验证边界

- `RunParkedDesktopExitAutoSaveTest` 在 `Draw3.DrawingController.cpp:1603-1728` 构造带真实 `InkCanvasCollection`、`InkStroke`、`BuildStrokeTileFootprint` 和 `CanvasRuntimeHistory::AppendStroke` 的 Desktop/PPT/空 Desktop；测试调用的正是生产 `CaptureDesktopAutoSaveForScene`，不是另抄的 policy 判定。它断言 active Desktop Exit、PPT/Whiteboard active 时 parked Desktop Exit 的 workspace GUID、page GUID、stroke 数及首点，非 Desktop Clear、关闭开关、空页零 callback。`IdtMain.cpp:288-289` 在配置/单实例/窗口初始化前分派 `--draw3-parked-desktop-exit-test`。该测试不经过生产 `DrawingController::Run` 的真实 slot swap、Host Stop/worker、UInk/index、非零 page index、多页、callback 拒绝或长文档成本。
- **red CLI exit 1（两项 parked Exit 失败）→ green exit 0、完整 Debug ARM64 Solution Build exit 0 是主 agent 的运行证据，不是本 reviewer 实测。** 我只读现有 stderr：red 为 PPT/Whiteboard parked Exit 两项失败，green 为 `PASS: production Desktop Exit source and snapshot`；本次仅独立执行目标三个文件的 `git diff --check`，exit 0。
- 真 GUI 的 Desktop→PPT/Whiteboard→正常退出及 worker durable UInk/index 严格回读、保存开关变化、提交拒绝/磁盘故障、活动 contact 收尾均未由该 CLI 证明。跨进程 Desktop 自动可见恢复仍是未开放能力；本补丁只解决 Exit 请求来源，不应升级为新进程画面恢复通过。

## 同期 CrashHandler 无窗口入口的附加只读核对

主 agent 在 `IdtMain.cpp:278-321` 另加精确参数 `--crash-filter-lifecycle-test`，位于配置、单实例、正常 `CrashHandler::Initialize` 和任何 HWND 初始化之前；未带该参数的默认启动不会调用测试函数。测试先保存进入时的 UEF，验证 `CrashHandler::Initialize/Shutdown` 的重复调用、旧过滤器为 null 与哨兵函数两种情况，成功及两条显式失败返回都把 UEF 设回进入测试前的值。`CrashHandler.cpp:37-67` 的 `g_filterInstalled` 门使重复调用不重复保存/恢复。CLI 没有制造异常、dump 或重启；它会改动本测试进程的 CRT invalid-parameter/purecall handler，随进程立即退出，不影响另一正常产品进程。静态未发现该测试误改默认启动或留下 UEF 状态；实际线程竞态、第三方覆盖 UEF 与真实崩溃路径不由此用例证明。主 agent 报告 Debug ARM64 Solution Build 和显式无窗 CLI 均 exit 0；本 reviewer 仅做上述源码核对，未复跑。

复查命令：`git show HEAD:Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`；`git diff --unified=5 -- Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cppm Inkeys/IdtMain.cpp`；`rg -n 'SelectDesktopAutoSaveSource|CaptureDesktopAutoSaveForScene|captureDesktopAutoSave|PrepareExitAutoSave|RunParkedDesktopExitAutoSaveTest' Inkeys/Inkeys/Drawing/Draw3 Inkeys/IdtMain.cpp`；`git diff --check --` 加上述三个目标文件。

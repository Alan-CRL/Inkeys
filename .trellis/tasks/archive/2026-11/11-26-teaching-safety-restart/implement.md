# C1.6a 实施计划

分工（用户 2026-11-26 指定）：**Claude 写代码，Codex 审查**（Sol High 只读）。收尾顺序：Claude 独立审查 → Codex 审查 → Claude 修复。子代理不并行。

构建：`InkeysRepo.sln`，`Debug|ARM64`，ARM64 原生 MSBuild，超时 ≥ 5 分钟（复用 `%TEMP%\inkeys-codex\c14\build.sh` 的写法）。源文件 UTF-8 BOM + CRLF，用 Edit 或 `perl -pi` 修改，不用 `sed -i`。

## 步骤

- [x] **S1 配置与设置页**（2026-11-26 完成）（design §2）
  - `Other.Config.cppm` 新增 `Application.CrashRecoverInk`（默认 true）；`Other.Config.cpp:747–754` 上限改为 2。
  - `Setting.Pages.cpp`、`Setting.cpp`：分组标题、三档 `ComboRow`、新开关；草稿与事件；clamp 0–2。
  - i18n：zh-CN 改文案、删 `Mode4`、加 `RecoverInk`；pwsh 7 跑 `Scripts/i18n.ps1 sync`，补译其余语言。
  - UEF：删 `crashMode == 3` 分支；提示档 `Show()` 返回 `Failed` 时也重启。
  - 门禁：构建；配置读取单元测试；设置页导航测试跑一次。
- [x] **S2 `Helper.Fatal` 模块与看门狗**（实为 `Helper.Fatal.h/.State.cpp/.cpp` 普通头 + 源文件，见 design §8）（design §3、§3.1）
  - 新增 `Helper/Helper.Fatal.cppm/.cpp`，加入 `Inkeys.vcxproj` 与 filters。
  - 判定逻辑抽成纯函数，`InkeysHeadlessTests` 覆盖 design §5 的边界。
  - `IdtMain.cpp`：启动门禁完成后 `StartWatchdog()`，退出时 `StopWatchdog()`。
  - `WriteCrashReportTxt` 识别 `kExceptionCode`，写出原因与 detail。
- [x] **S3 Windows 回调补漏**（design §3.5）
  - `InstallCrtHandlers()` / `RestoreCrtHandlers()`，首次安装保存旧值，部分安装可还原，`Shutdown()` 先还原 CRT 再恢复 UEF。
- [x] **S4 接入点**
  - Draw3 意外停止：`IdtMain.cpp:4265–4290` 改为 `RaiseNow(Draw3Stopped)`（design §3.4）。
  - 已知失败强退链：`PublishFatalFailedCleanupNoWait` 运行期按档位抢 `CrashRestart`（design §3.7）。
  - 卡死检测：UI3 调度器与 Draw3 `Run` 的 Beat / Idle 点（design §3.2 表），Host 清理段 Beat。
  - 持续失败：逐个客户端列出真实故障分支并接入 `RecordFailure / RecordSuccess / ClearFailure`（design §3.3 表），把清单写进本文件附录，交 Codex 复核。
- [x] **S5 验证**（实机项待 C1.6b 完成后与用户联合验证）
  - 构建；`InkeysHeadlessTests` 全部通过；`ShutdownSupervisor` 的 `crash-*` 隐藏场景、`Bar.Presentation.Test` 通过。
  - 新增隔离子进程测试：看门狗线程、主线程、`SIGABRT`、worker `terminate`、`noexcept` 违约、invalid parameter、purecall 各自命中 UEF，Debug 与 Release ARM64 各跑一次（design §5）。
  - 热路径检查：`Beat / Idle / RecordFailure / RecordSuccess / ClearFailure` 只是原子写，不增加锁、分配或日志。
  - 实机：等 C1.6c 完成后联合验证三档 × 三种触发。
- [x] **S6 审查**（Codex 两轮：`research/codex-impl-review-20261126.md`、复审结论见 design §9 末行）：Claude 逐行审 diff → Codex 只读审查（简报附 design.md 与 S4 故障分支清单）→ 修复 → 再构建。

## 回滚点

每一步完成后都能单独构建。S4 的各接入点相互独立，出问题可以单独撤回某一处，不影响 S1–S3。

## 附录：S4 客户端故障分支清单

（2026-11-26 实施。“失败”只计真实 API 失败；共享设备丢失只计 `UiDeviceRecovery`；延期、退避、等回执、owner 竞争都不计。所有槽另有 5 秒陈旧窗口：最近一次失败超过 5 秒视为客户端已停止尝试，不触发，再失败时重新计时。）

| FailureSlot | 文件与分支 | Record | 说明 |
|---|---|---|---|
| UiDeviceRecovery | `RenderPipeline.cpp` 调度器 `recoveryPending` 块，`deviceRecovery()` 返回值 | 失败 / 成功 | 唯一的共享设备计数点 |
| UiBar | `Bar.RenderLoop.cpp` `EnsureDeviceResources` 失败且非共享设备丢失 | 失败 | 用户举例的“主栏无限重试”路径 |
| UiBar | 同文件 延期呈现分支 `EndFrameDraw` 失败且非设备丢失 | 失败 | `sourcePresentationDeferred` 不计（非 API 失败） |
| UiBar | 同文件 `CompleteAttempt` 后：Committed 成功；Retry/RecreateTarget 且非设备丢失为失败 | 失败 / 成功 | Deferred（装饰租约让出）不计 |
| UiBar | `BarRenderLoopCoordinator::Unregister` | 清除 | |
| UiBar（不计） | 布局越界 `!candidateGeometryValid` | — | 逻辑条件，不是 API 失败 |
| UiSetting | `Setting.cpp` `ResolveSettingResourceFailure`（全部调用点都是真实失败）且非共享设备丢失 | 失败 | 遮挡探测的正常 `Retry` 不经过此函数 |
| UiSetting | 同文件 Present 成功分支（只认 `S_OK`；窗口不可见时不累计并清槽） | 成功 | |
| UiSetting | 设置会话结束（协程退出循环后） | 清除 | |
| UiPageControl..4 | `PageControl.cpp` `PresentScene` 后：Success 且 `pixelsSubmitted` 为成功；Retry、非延期且有明确 API 失败（`failureHr`/`resourcesFailed`/非无效 HWND 的 `failureError`）为失败；隐藏时清除 | 失败 / 成功 | 每个表面一个槽（`PageFailureSlot`） |
| UiWhiteboard | `Whiteboard.cpp` 背景 `EnsureDeviceResources` 失败且非设备丢失 | 失败 | |
| UiWhiteboard | 同文件 `GetDC`/`ReleaseDC`/`EndFrameDraw` 失败或 ULW 真实失败（`SubmitFreezeSurface` 的 `apiFailed`） | 失败 | 表面被替换不计；`ReleaseInactiveBackgroundResources` 清除 |
| UiWhiteboard | 同文件 发布背景候选后 | 成功 | |
| UiSelection | `SelectionOverlay.cpp` 资源失败（非 `D2DERR_RECREATE_TARGET`）、Present 失败 | 失败 | `SetBounds/Show` 失败不计（窗口命令） |
| UiSelection | 同文件 `CommitPresented` 成功 / 隐藏 | 成功 / 清除 | |
| UiStartupPreview（不接） | — | — | 只在启动期运行，启动期不受看门狗监督 |
| Draw3Present | `DrawingController.cpp` `PresentFrame`：`presentation_.Present` 返回值（false 均为真实失败，已核实 `TransparentPresentation.cpp` 各分支都 `SetFailure`） | 失败 / 成功 | 活动帧与空闲帧共用 |
| Draw3Present | 同文件 `PresentInitialCanvas`/`PresentFullCanvas` 合成失败 | 失败 | |
| Draw3Present | 同文件 帧尾 `rasterSubmissionFailed` | 失败 | 光栅失败帧没有 Present |
| Draw3Present | 同文件 `CompleteUlwBeforeIdle`/`ServiceUlwBeforeFrameWait`/`BeginUlwFrame` 失败；成功以 `CommittedPresentCount()` 推进为证 | 失败 / 成功 | 帧外 ULW 出口 |
| Freeze | `IdtFreezeFrame.cpp` `submitSurface`：ULW 成功 / `apiFailed` 失败（白板接管拒绝不计） | 失败 / 成功 | 覆盖原先丢弃结果的各调用点 |
| Freeze | 同文件 运行时 `surfaces.Ensure` DIB 分配失败 | 失败 | |
| Freeze | 同文件 退休成功或交给白板后 | 清除 | 退休提交失败时保留计时 |

卡死检测（Beat / Idle）：

| Slot | Beat | Idle |
|---|---|---|
| UiScheduler | 循环顶；每个控制任务前；等待返回后；帧节拍等待后；每个客户端回调前 | 紧挨 `WaitForSingleObject(wakeEvent)` 前；调度线程退出时 |
| Draw3Loop | `Run` 循环顶；`processCommandAndReconcile` 入口（每条命令）；Host 在 `Run()` 返回后、清理前 | 两处 `metrics_->BeginIdle` 之后（空闲分支与活动接触静止等待，含橡皮空闲等待） |

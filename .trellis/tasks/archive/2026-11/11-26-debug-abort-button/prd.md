# C1.6c 调试页 abort 测试按钮

父任务：`11-26-inkeys-crash-recovery`。

## 目标

在设置窗口“调试软件”页（`UI/Setting/Setting.cpp` 的 `settingTabEnum::tab9`）加一个手动触发崩溃的入口，用来验证 C1.6a 的自动重启和 C1.6b 的墨迹恢复。

## 需求（草案，待确认）

- 新增一个分组（暂名“崩溃恢复测试”），里面是触发按钮，沿用现有 `Design::ButtonGroupRow` 等组件。
- 用户确认（2026-11-26）：不调用真正的 `abort()`，改为**野指针访问**触发（访问违规 → SEH → UEF，即 Windows 崩溃回调路径）。按钮文案仍可叫“abort”或“触发崩溃”（待定）。
- 用户确认（2026-11-26）：放**三个按钮**：
  1. 野指针崩溃（Windows 崩溃回调路径）；
  2. 模拟软件内致命错误（Draw3 意外停止，走 C1.6a 的第二条路径）；
  3. 模拟卡死（UI3 线程无响应超过 15 秒，验证看门狗）。
- 误触保护：默认每个按钮点击后先弹 `Inkeys::UI::MessageBox` 的 OkCancel 确认，确认后才触发（用户未另行指定）。
- i18n：新增文案先写 zh-CN，其余语言按项目流程同步。
- 依赖 C1.6a 的 `Helper.Fatal` 与看门狗，排在 C1.6a 之后实施；分工同 C1.6a（Claude 写，Codex 审查）。

## 实施要点

| 按钮 | 触发方式 | 走的路径 |
|---|---|---|
| 野指针崩溃 | 在设置窗口线程写一个运行期得到的空指针（`volatile` 防优化） | 访问违规 → UEF |
| 模拟致命错误 | Draw3 产品接口新增调试注入标志；`DrawingController::Run` 循环顶部看到标志就抛 `std::runtime_error` | Host 捕获 → Draw3 停止 → 主循环 `RaiseNow(Draw3Stopped)` |
| 模拟卡死 | 经 `RenderPipeline` 的 `PostControl` 投递一个 `Sleep(20000)` 的控制任务 | UI3 调度器忙碌超过 15 s → 看门狗 `ThreadHang` |

- 放在 `Setting.cpp` 的 `settingTabEnum::tab9`，新分组用 `Design::ButtonGroupRow`。
- 调试器附加时看门狗不触发（C1.6a 设计），所以“模拟卡死”要在不挂调试器的情况下测。

## 验收（草案）

- [x] 点击后进程按当前“崩溃处理方式”处理：提示并重启 / 静默重启 / 直接退出。（2026-11-27 实机通过）
- [x] 自动恢复开启时，重启后墨迹恢复（与 C1.6b 联合验证，2026-11-27 实机通过）。

## 实施记录（2026-11-26）

- 设置“调试软件”页新增分组“崩溃恢复测试”（`Design::ButtonGroupRow` `##crash-recovery-test`），三个按钮：野指针崩溃、模拟绘制致命错误、模拟界面卡死；i18n `SettingsUI.DebugSoftware.CrashTest.*`（20 种语言已补齐）。
- 点击后入队 `SettingBusinessKind::ConfirmCrashTest`，在**业务 worker** 上弹 OkCancel（默认“取消”），不占 UI3 调度线程（否则确认框等待本身会被看门狗判为卡死）。
- 野指针：worker 线程写 `int* volatile` 空指针，真实访问违规进 UEF。
- 模拟绘制致命错误：`Draw3::InjectProductDebugFatal()` → `Host::InjectDebugFatalForTest()` 置 `Fatal::RequestDraw3DebugFault()` 并 `PublishControlWake`；`Run` 循环顶与 `processCommandAndReconcile` 入口消费标志后抛 `std::runtime_error`，经 Host 停止 → 主循环 `RaiseNow(Draw3Stopped)`。
- 模拟界面卡死：看门狗已启动时 `RenderPipeline::PostControl` 投递 `Sleep(20000)`；未启动或被拒绝时只写 Warn。
- Codex 审查：并入 C1.6a 第二轮（`../11-26-teaching-safety-restart/research/`），帧内唤醒被消费的问题已修。实机验证待 C1.6b 完成后联合进行。

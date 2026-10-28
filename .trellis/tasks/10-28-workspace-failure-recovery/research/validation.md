# 实现与验证记录

## 四项实现

1. `IdtState.cpp`：只有 `CommandPhase::Rejected` 才恢复 Whiteboard 源 expanded、页快照、Freeze ownership 和背景 active；Pending/failed 与输入 fence 保持，恢复屏障未退休时不显示。
2. `WorkspaceWaitTracker`：背景 inactive、目标 ready、背景 prepared 和 final active 的无进展等待进入既有失败预算；输出/内容/PPT UI-ready/背景实际进展才重置等待。
3. `DCompRetirementSlot`：默认 1000ms 有界退休；成功/失败/超时/忙碌明确区分，禁止第二 worker，迟到完成只清理旧槽。退休失败不卸载 dcomp.dll。
4. 样式允许时走原 ULW；样式/初始化失败由 `RequestProductLegacyRecovery` 触发完整 `StartDraw3ProductWithFallback` 重建。`ERROR_TIMEOUT` 不触发重建，避免超时后继续创建新 HWND。

## 生产回归

- `--state-mode-production-test`：源预览恢复条件、等待进度、legacy 重建回调通过。
- `draw3_bridge_tests`：退休成功、失败、超时、第二 worker 拒绝、迟到释放和下一次退休通过。
- 完整 `InkeysRepo.sln Debug|ARM64`：`Build/WorkspaceFailureRecovery/final-msbuild.log`，退出 0。
- `InkeysHeadlessTests.exe --no-window`：退出 0，stderr 为空，末尾 `PASS animation correctness`；日志 `Build/WorkspaceFailureRecovery/headless.*`。
- `git diff --check`：通过。

## 静态复核边界

- 正常三态、owner 树、默认绘制、存储格式、PptCOM ABI 未改。
- 退休超时不会重建；样式可清除时不强制重建；只有完整 Host 停止后才调用 legacy restart。
- 新 Host 必须重新取得 generation、目标场景和真实首帧，旧 completion 不确认恢复。
- Win7 无 DComp 路径未增加静态 DComp 依赖。

## 未执行

未进行真实 DComp 故障注入 GUI、Office/WPS、Win7 SP1+KB2670838 到 Win11 分平台实测。离屏结果不证明这些系统/驱动组合全部通过，任务保持 in_progress，未 commit/push。

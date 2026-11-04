# Implement
1. `IdtState.h/.cpp`：新增 `WhiteboardSwitching()`。
2. `Bar.KeyboardShortcuts.h`：modal 改用 `WhiteboardSwitching()` + 最小化；分离式 X 在白板直接 Select。
3. `IdtState.cpp::ChangeStateModeForShortcutIfRevision`：门禁改为 `WhiteboardSwitching()`。
4. 测试：`Bar.KeyboardShortcuts.h` ProductionTest（`Inkeys.exe --state-mode-production-test`）补白板稳定态 X/分离式不清空/过渡态拒绝用例；headless 键盘测试回归。
5. spec `keyboard-shortcuts.md` 去掉“普通白板快捷键暂停”，写入新合同。
6. 构建 `InkeysRepo.sln` Debug|ARM64；`InkeysHeadlessTests --no-window`；`Inkeys.exe --state-mode-production-test`。

7. 按用户最终要求迁移动画速率到实验选项页，保留原配置/范围/实时预览/释放与离页保存；同步退休事件与设置合同。
8. 接续阶段仅执行不创建 HWND 的 CLI/离屏验证；不重复原 agent 的真实窗口测试。

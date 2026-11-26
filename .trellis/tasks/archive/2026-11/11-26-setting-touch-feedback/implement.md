# 实施计划：设置窗口禁用触摸右键与点击反馈

## 步骤

1. `Inkeys/Inkeys/UI/Setting/Setting.cpp`
   - `ImGuiWndProc` 的首个 `switch` 新增 `case WM_TABLET_QUERYSYSTEMGESTURESTATUS:` 返回四个 `TABLET_DISABLE_*` 位（与 `SelectionOverlay.cpp:231` 相同）。
   - 新增 `ConfigureNativeTouchFeedback(HWND)`：动态取 `SetWindowFeedbackSetting`，逐项关闭 design 中列出的反馈类型；关键处加中文注释说明 Win7 跳过与失败忽略。
   - 在 `ImGuiWndProc` 的 `WM_CREATE` 调用该函数（实施时改定，见 design“落点”；不改 `Setting.cppm` / `IdtMain.cpp`）。
2. 检查 `git diff`：BOM + CRLF 保持、无无关改动。

## 验证

- 构建：ARM64 原生 MSBuild 构建 `InkeysRepo.sln` Debug|ARM64（超时 ≥ 5 分钟）。
- 不新增自动化测试（纯窗口属性，无可测纯逻辑）。
- 实机（用户执行或确认）：PRD 验收清单前三项。

## 回滚点

- 单提交；回滚即撤销该提交。

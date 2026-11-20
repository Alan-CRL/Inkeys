# 实施计划

分工（用户 2026-11-20 指定）：
- **Claude**：编写全部代码、构建与测试、独立审阅。
- **Codex**：`gpt-6.2-sol` High + fast，只读，负责调查和审阅。直连不通时，为该进程设置 `HTTPS_PROXY=http://127.0.0.1:7890`。

## 通用约束

- 最小修改，未列出的行为一律不改。
- 改动的 `.cpp/.cppm/.h` 用 UTF-8 BOM + CRLF；locale `.jsonc` 无 BOM、CRLF。不改文件末尾换行。
- 新测试不得放进未定义宏的 `#if` 中（headless 没有定义 `DRAW3_TESTING`）。
- 构建：ARM64 MSBuild 编译 `InkeysRepo.sln`，Debug|ARM64，`MSBUILDDISABLENODEREUSE=1`。遇到 LNK1000 就删除 `Inkeys.ilk`。
- 测试：
  - headless：`InkeysHeadlessTests.exe --no-window`。
  - 隐藏窗口和 Controller 自检：从 `Build/ARM64/Debug` 目录用 `& exe … | Out-File` 调用。
- i18n：先改 `zh-CN.jsonc`，再用 pwsh 运行 `Scripts/i18n.ps1 sync`，补齐翻译后运行 `check`。

## 阶段

### S1：R1 + R2（小改动）

- 改动：`Draw3.WindowControl.cpp`（反馈设置与恢复）、`Setting.cpp`（信息条）、i18n 20 种语言、`setting_design_tests` 按 ID 断言。
- 验收：构建通过，headless 通过，i18n check 通过。

### S2：R3

- 改动：
  - 配置项与设置页开关、启动时注入、窗口服务命令。
  - `Window.CanvasPolicy.h` 的 `keepNonFullscreen` 和 `EnsureCanvasPolicy` 的按角色解析，并纳入缓存键与准备校验。
- 测试：`window_tests.cpp` 改成“角色 × 开关 ×（预留的）单窗口选择态”矩阵，保留原区域和负坐标断言；`config_metadata_tests` 补缺键、类型、往返和重置用例。
- 更新 spec：`settings-navigation.md` 和 `draw3-integration.md`。

### S3：R4

- 改动：
  - `Bridge::SelectionUsesAuxiliaryOutput` 增加 `auxiliaryAvailable`，并更新所有调用点；运行快照提供实际后端。
  - `ResolveDrawpadPresentationSurface` 的单窗口分支；IdtState 与 Window 的显隐和安全放行。
  - TransparentPresentation：初始化时跳过辅助 presenter、回退交接、调试文本。
  - DrawingController 拆分 `outputTargetChanged`。
  - 底行策略接入“ULW 单窗口选择态”。
- 测试：
  - 更新 `draw3_bridge_tests.cpp:838–921` 和 HiddenWindowTest 中断言旧行为的用例，改为按呈现方式区分。
  - 补测：ULW 空页及有墨时的往返切换、辅助窗口零分配、仅切换样式时不整帧、按住时切换、回退时旧回执失效。
- 更新 spec：`draw3-integration.md`、`native/platform-and-resources.md`。

### 收尾

1. Claude 独立审阅完整 diff。
2. Codex（Sol High + fast，只读）做完整审阅。
3. Claude 逐条核实（P1 用反证实验验证）后修复，再跑一轮构建和测试。
4. 用户真机验收：
   - WM_POINTER 下按下圈和拖尾消失。
   - 桌面绘制态下边缘滑入被屏蔽，选择态下任务栏能升起；切换开关后行为与原来一致。
   - ULW 单窗口切换不闪烁；Win7 启动正常。
5. 提交需等用户授权。

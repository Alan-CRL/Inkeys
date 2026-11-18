# 第2步-A2 实施计划

> 分工：Claude 编写全部实现；Codex（gpt-6.1-sol / high / fast，只读）完成后审阅；Claude 独立审阅 → Codex 审阅 → 修复 → 复验。不并行写入。未经用户授权不提交、不 push。

## 步骤

0. **开工**：`python ./.trellis/scripts/task.py start 11-17-cursor-frame-pacing`；`git status` / `git log` 确认在 `refactoring/draw` 且 HEAD 含 `0c8a7337`。先完成 design 中的“开工核对项”（控制标记处理开销、拒收接触时主光标生成、橡皮 F3 idle 等待依据、`DiscardUntilTerminal` 后快照可读性、设置即时下发链路）。任何一项与设计矛盾 → 停下向用户报告，不自行改方案。
1. **ContactInput**：`PublishControlWake(bool interruptFrame)` 与对外 `PublishPacedWake()`；对应单测（design §6 #1）。
2. **WindowController**：`RequestDrawingCursorRenderPaced()`、`unthrottledMouseMoveWhileDrawing_` 及 setter；改 `PublishMouseCursorSample`、`PublishPenCursorSampleImpl` 的请求条件；单测（§6 #2、#3）。
3. **DrawingController 帧尾**：光标接触节流分支（design §4.1）；单测（§6 #4）。
4. **拒收触摸光标**（design §4.2）：先写验证用测试确认快照可读，再实现登记/读取/清理；单测（§6 #5）。
5. **设置项**：配置键、`CursorSettings` 字段与打包位、`IdtState` 读取、Host 下发、设置页 ToggleRow、zh-CN 文案；`pwsh 7` 运行 `Scripts/i18n.ps1 sync`，补译 19 种语言，`check` 通过。
6. **更新既有测试预期**（design §6“更新”），只改预期、不改产品意图。
7. **压力工具**：`Scripts/StressLoad/StressLoad.cpp`、`Scripts/Start-StressLoad.ps1`、`Scripts/Start-StressLoad.cmd`；用脚本编译 ARM64 与 x64 各一次确认可编译；短时运行确认施压、显示与关闭退出（启动 GUI 需用户同意，否则只编译，交用户运行）。
8. **构建与测试**：`InkeysRepo.sln` `Debug|ARM64`（ARM64 原生 MSBuild，`-t:Inkeys;InkeysHeadlessTests`，超时 ≥ 5 分钟，PowerShell 内 `Remove-Item Env:PATH`、`MSBUILDDISABLENODEREUSE=1`，日志 `-Encoding utf8`）；先跑新增/相关单测，再后台跑完整 `InkeysHeadlessTests.exe --no-window`（约 5 分钟）；`git diff` 检查 BOM/CRLF 与无关改动。
9. **审阅**：Claude 独立审阅 → Codex 只读审阅（附 diff 文件、prd/design、已接受偏差清单）→ 修复 → 复验构建与相关测试。
10. **交付**：向用户汇报（含未验证项）；实机测试由用户执行（30/120 FPS 鼠标书写、准入阻塞/关闭多指拒收、压力工具下的高帧率书写）。实机通过后更新规范（`draw3-cursors.md`、`input-and-ink.md`、`settings-performance-and-drawing.md`），经授权后提交。

## 回滚点

- 步骤 1–3 为一组（唤醒 + 窗口条件 + 节流分支），新选项可在运行时恢复鼠标旧行为。
- 步骤 4（拒收触摸）、5（设置）、7（压力工具）可独立回退。

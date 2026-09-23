# Implementation and validation
1. Implement the approved shared API and deterministic pure tests; inject mode/display scale through Host/WindowController.
2. Adapt ink_prediction alias and width interval; batch-latch scale, preserve hover and reconnect ownership, use accepted radius for contact cursor.
3. Register shared source/test in existing projects and headless runner. Do not edit the old inkStrokeModelerTest demo.
4. Main runs the full solution once the main-session implementation is ready. Locate ARM64-native MSBuild using current VS installation; in the SAME invocation run Remove-Item Env:PATH -ErrorAction SilentlyContinue and set MSBUILDDISABLENODEREUSE=1. Use at least five minutes; no visible process windows.
5. Run Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window; review actual failures and fix causes, not assertions to hide failures. Static review includes downstream radius consumers, fake reconnect speed, time ordering, latch lifetime, DPI/revision notification, callback shutdown.
6. Update the code spec with implemented signatures, defaults and concrete tests. Keep tasks in progress; no commit or archive. Record physical device feel and Windows7 runtime compatibility as not empirically tested on this machine.

Recovery override: all steps are executed by the main session only; no subagents. See validation.md for actual results and remaining manual device acceptance.

## 2026-09-23 当前轮次追加计划

1. 保留本任务历史文件与上下文，追加 Touch 场景合同和独立研究记录；当前 `bugfix/eraser`/`94e07b2` 为实现基线。
2. 在 headless/隐藏窗口补旧物理路径红灯、大屏普通与分级清扫、Surface/中间尺寸、手动/自动/回退、尺寸/增益/面积、诊断归属回归；旧跨场景同速等同断言改为同场景单位正确性。
3. 只在 `ResolveConfig` 集中解析 Touch 物理场景；控制器共用有效阈值，Host/Controller 帧级诊断补可观察字段。
4. 按仓库要求构建完整 `InkeysRepo.sln Debug|ARM64`，运行 headless `--no-window`、`--draw3-eraser-hidden-test` 和适用产品隐藏验证；检查 diff、编码与用户已有文件。
5. 更新 spec、本轮验证及 Trellis 会话记录；按用户后续授权提交本轮改动，保持 `in_progress`，不推送、finish 或归档；真人 Surface/教室验收另列。

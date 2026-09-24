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

## 2026-09-23 控制台诊断扩展

1. 复用原配置键，更新三语言设置文案并运行 i18n sync/check。
2. 在 Host 的现有显示快照入口输出 EDID/分辨率/DPI，在 DrawingController 的选中 contact 诊断中补真实坐标/设备/尺寸归属；Host 限频输出统一输入行，保留原面积明细。
3. 补隐藏窗口接入断言，执行完整 `InkeysRepo.sln Debug|ARM64`、headless 和专项隐藏测试，分别记录代码结果与原有窗口 owner 失败。
4. 更新 spec、独立验证记录与会话记录；按用户后续授权提交并推送本轮改动，保持 `in_progress`，不 finish 或归档。

## 2026-09-25 暖状态短快划轮次

1. 在当前基线补可重复的暖状态时序测试，先输出旧 Touch/ScreenPen/Mouse 的全程轨迹摘要及代表 CSV；旧基线不切换 Git 版本。
2. 仅调整 Touch/ScreenPen 增长参数，保留场景解析、Mouse、精细、面积和尺寸会话；必要时凭不可达测试再做局部修正。
3. 扩展速度、时长、采样率/帧率、状态、面积及产品几何回归；核对受授权替代的旧断言而不放宽其他容差。
4. 完整构建 `InkeysRepo.sln Debug|ARM64`，运行 headless、专项隐藏测试，记录退出码和原有 Window Service owner 失败；更新 spec、研究/验证和主会话日志，按用户后续授权提交并推送本轮改动，保持任务 in_progress，不 finish 或归档。

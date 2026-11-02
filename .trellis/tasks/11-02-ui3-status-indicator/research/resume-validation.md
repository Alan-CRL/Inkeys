# 2026-11-02 接续记录（阶段 2、3 完成，待人工观感审阅）

- 接续原任务 `11-02-ui3-status-indicator`，当前 session 已通过 `task.py validate/start` 关联，状态保持 in_progress。
- 本轮范围为阶段 2、3，遵守 implement.md 的人工观感审阅关口；阶段 4、5 的配置、正式状态源和软件版本直达仍待后续。
- 阶段 1 原有模型/测试以及测试工程登记已核实；修复首次 Advance 前过滤/点击导致 snapStack 残留、持续续帧的问题，并加两条真实模型回归。
- 设置软件调试页新增随机状态增删，删除只针对最新持久调试状态；文案覆盖 11 种语言，i18n check 全部 698/698。
- 保留 zh-TW.jsonc 原 LF；其余本轮触及的现有源文件保持检出时 CRLF/原 BOM。

## 已执行验证

- Python：`C:/Program Files/KiCad/10.0/bin/python.exe`（3.11.5）；WindowsApps 的 python 别名在当前 runner 不可执行。
- MSBuild：vswhere 定位当前 VS 安装后使用 `MSBuild/Current/Bin/arm64/MSBuild.exe`。同一 PowerShell invocation 先 `Remove-Item Env:PATH`、`MSBUILDDISABLENODEREUSE=1`；使用隐藏进程、10/15 分钟截止，不打开 GUI。
- 无窗口目标构建：`InkeysRepo.sln /t:InkeysHeadlessTests /p:Configuration=Debug /p:Platform=ARM64 /m:1 /nr:false`。沙箱内 C1902；核对 cl/c1xx/mspdb140 均为 14.44.35229 同源，沙箱外相同命令退出 0，定位为 Codex 沙箱下 PDB 服务限制，未改工程或工具链。
- `InkeysHeadlessTests.exe --no-window`：TEMP/TMP 仅为测试进程指向仓库 Build/status-indicator-validation；退出 0，PASS animation correctness，含新状态模型测试与空闲回归。
- 完整 solution 初次编译发现新增 Bar 接线错误（WakeInteractionWorker 可见性、DrawPointLightFrame protected、IdtAtomic 条件表达式），均已修复；后续完整构建退出 0，参见 solution-build-verified.log。
- 原始日志在忽略目录 `Build/status-indicator-validation/`。

## 本轮审阅要点

- 输入 worker 仅读成功呈现快照/入队，render owner 独占模型；Grip 变换与命中同源。
- 进度环优先于普通整环；多模拟下载只选择单一进度 owner，避免每帧换 key 回零。
- 后排胶囊文字/图标都隐藏，只露颜色；悬停复用 Showing/驻留淡出规则和抬手后移动抑制。
- Fan 首帧保留 Back 包络、收起尾段正确清除；描边/光影与旧新几何均纳入 dirty。
- 尚未运行任何 GUI、Computer Use，未创建 commit/push。

## 最终验证与已知限制

- 新增 Fan 退场连续性修复：注销/过滤/驱逐前冻结旧弧的 count 和各项 slot，收起归零后释放；普通 dock 补位仍用弹簧。新回归覆盖 dt=0 中心连续、首段小位移、连续注销和最终 idle。
- `InkeysHeadlessTests.exe --no-window` 最终复跑：退出 0，PASS animation correctness；含上述新回归。日志 headless-tests-final.log/.err.log。
- 新状态生产专项通过：真实命令队列、粗体 DWrite 测量（中文四字不溢出）、D2D 绘制、未呈现不可点击、取消、胶囊/轨道/Fan、业务命中角色、旧图标隐藏、进度门禁；没有 StatusIndicator FAIL。输出 Build/eraser-b/status-indicator/fan.png 已用本地图片查看器检查。
- 整体 `Inkeys.exe --bar-eraser-offscreen-test` 退出 1，仅失败 `PageSurface dark/light pixels follow the same UI3 surface color as the main bar`。新增状态专项本身无失败。
- 归因：HEAD 中旧断言对深浅主题统一使用 BarMainBarFillOpacity=0.80；HEAD 与当前生产 Bar.Scene/Theme/ThemeMaterial 均使用浅色默认 0.88，浅色 alpha≈224 而旧断言期待204。三份生产文件本轮无改动，属于既有主题测试预期不一致。
- 隔离对照：仅临时禁用新增状态专项调用和截图块、重新完整构建，再运行同一离屏入口，仍退出1且同一唯一失败。对照日志 offscreen-control-results.log。随后测试源码从备份逐字节还原并核对；没有保留禁用代码，也没有修改无关分页生产或断言。
- 最后完整构建为 solution-build-delivery.log；为构建链的 pwsh 脚本仅在进程的单一 Path 中补当前 pwsh 目录，未改仓库配置。
- i18n check：十一语言一致，其他十语言均 698/698；diff --check 通过；BOM 与检出 EOL 保持，zh-TW 原 LF 保留。
- 静态检查由 trellis-check 全范围审阅，并在最后模型/进度光影修改后做增量复核，未发现剩余确定缺陷。
- 尚未进行真实 HWND 缩窗/残影、鼠标/触控/实体笔与视觉参数验收。人工步骤在 manual-review.md。阶段4/5仍按既定审阅关口等待用户观感确认，任务保持 in_progress；未运行GUI/Computer Use，未commit/push。

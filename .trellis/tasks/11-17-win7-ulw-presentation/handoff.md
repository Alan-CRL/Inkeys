# 第2步交接说明（2026-11-17，上下文压缩前）

## 一句话状态

第2步已拆为 A / B / C 三个子任务（另加 A2）。**以文末“恢复指引（2026-11-18）”为准：A、A2 已完成归档，下一项为 B。** 以下为历史记录。

## 环境与协作约定

- 仓库 `D:\Project\Inkeys\Repo\Inkeys`，分支 `refactoring/draw`（HEAD 7047db94，开工前先 `git status` / `git log` 确认）。第1步在 `refactoring/drawpad-ui` 并行（UI3/主栏改动）；本步骤会改 `Setting.cpp` 与 i18n，合并时注意小冲突。
- 当前 Trellis 活动任务：`.trellis/tasks/11-17-dcomp-waitable-latency`（status=planning）。开工时执行 `python ./.trellis/scripts/task.py start 11-17-dcomp-waitable-latency`。
- 分工：**Claude 写全部实现**；Codex 只做前期调查与完成后审阅（只读）。收尾顺序：Claude 独立审阅 → Codex 审阅 → 修复 → 复验。
- Codex：本会话 Fast 可用、Astra 不可用；命令 `cat brief.md | codex.exe exec -C <repo> -m gpt-6.1-sol -c model_reasoning_effort=high -c service_tier=fast --sandbox read-only -o report.md -`，简报放 `%TEMP%\inkeys-codex\<主题>\`。
- 构建：`InkeysRepo.sln` `Debug|ARM64`，ARM64 原生 MSBuild，`-t:Inkeys;InkeysHeadlessTests`，超时 ≥ 5 分钟；PowerShell 内 `Remove-Item Env:PATH` 规避重复键、`MSBUILDDISABLENODEREUSE=1`，日志 `-Encoding utf8`。
- 源文件编码：改动的 `.cpp/.cppm/.h` 必须 UTF-8 BOM + CRLF；Git Bash 的 `sed -i` 会把 CRLF 改成 LF，改完用 `sed -i 's/\r*$/\r/'` 还原并 `file`/`git diff --stat` 核对。
- 不 commit、不 push，除非用户明确授权。

## 关键事实（已由 Claude 与 Codex 交叉核对）

详见 `research/presentation-pipeline-current.md`（流程图 §1、L0/L1/L2 §2、脏区 §3、ULW 串行 §4、ULW 方案对比 §5、合成后各方案步骤 §6、DComp CPU/GPU 重叠 §7、waitable 与排队控制 §8）。要点：

1. 最终目标 `backBufferTexture/RTV` 现为 `swapChain->GetBuffer(0)`；ULW 模式也建双缓冲 FLIP 链但从不 Present。`InkRenderer::CreateSizeDependentResources(nullptr, …)` 已有建普通纹理的分支（R:456–466），仅 `Resize()` 要求交换链——这是阶段 B 的切入点。
2. `frameDirty` 每帧从空开始、合并为单个包围矩形；ULW 失败后无累计脏区，而是 `RequestFullPresent()` 下一帧整帧。
3. DComp 已有 CPU/GPU 重叠（D3D11 异步命令队列，每帧路径无 CPU 读 GPU）；ULW 的 `Map(READ)` 紧跟 Copy，强制 CPU 等 GPU。
4. Win8.1+ DComp 交换链带 waitable 标志并设上限 1，但**从未调用 `GetFrameLatencyWaitableObject`**；带标志后设备级上限不适用，`Present1` 是否仍按上限阻塞未知。Win8.0 回退为不带标志的 FLIP，由设备级 `SetMaximumFrameLatency(1)`（GraphicsInitialization:156）限流。Win7 走 ULW，不 Present。
5. 所有 `Present1` 唯一出口 P:1161，经 `PresentFrame`（C:15335）；三个调用点：清屏 C:15432（启动时 Host:2338 在 `Run` 前调用，首帧失败判启动失败）、整画布 C:15466、主循环 C:23338。空裁剪矩形 P:1157 不调 `Present1` 但返回成功。
6. 帧率仅启动时选定（30/60/120/240，`configuration_.timingProfile.target_fps`）；未见刷新率获取代码。
7. 画布调试 `CanvasDebug.cpp:754` 以 `workSumMs` 算无限制帧率与负载百分比。

## 阶段 A 已确认决策（详见 `11-17-dcomp-waitable-latency/design.md`）

- 令牌语义；等待点 C:21598 附近（恢复/Resize 后、输入 Drain 前）；帧循环外的 Present 无令牌时在 `PresentSwapChain` 内补等。
- **超时后仍合成并 Present**。
- **自适应超时** `clamp(1.5 × max(1000/target_fps, 刷新间隔, 信号间隔EMA), 4, 200) ms`；GPU 慢时等待照常成功，不越排越多。
- **熔断**：可计数状态下连续超时 ≥ 8 且 ≥ 1000 ms 未获取 → 本会话重建为不带标志的 FLIP（仍由设备级上限限流）。隐藏/最小化/恢复/交接/SelectionUlw 期间不计数。
- **设置开关**：性能页“渲染与兼容”节，DComp 开关后新增“waitable 模式下启用等待”（说明：在 Windows 8.1 及以上系统中，防止帧积压造成的延迟），默认开、重启生效；关闭 = 现状（带标志、不等待）。配置键 `Experimental.Inkeys3.Draw3.EnableWaitableWait`。
- 诊断仅日志（Debug 级别，默认不输出）+ 运行指标；等待耗时不计入工作耗时（画布调试负载与第2步-C 口径）。
- Win8.0 / 未来 Win7 DWM 方案：不加应用侧等待，仅 Debug 指标记录 `Present` 耗时。不引入任何 GPU 查询自旋或信号量兼容方案。

## 下一步

1. 用户指示开工后：`task.py start 11-17-dcomp-waitable-latency`，按 `implement.md` 步骤 1–9 执行。
2. A 完成并经用户实机 A/B 后，规划 B（`11-17-ulw-texture-pipeline/prd.md`）。
3. C（`11-17-perf-dashboard/prd.md`）在 A/B 之后详细调查；A/B 实现时满足其“对 A/B 的前置要求”。

## 仍待用户确认 / 实测

- 预设帧率高于刷新率时的默认策略（实现后用开关在 60Hz / 120Hz 屏实测再定）。
- 熔断事件日志级别目前按用户要求为 Debug；若希望默认日志可见可改 Info。
- `Inkeys/Memery` 目录名拼写（用户原文 “Memery”）。

## 进展（2026-11-17 晚，阶段 A 实机测试后）

- 阶段 A 已提交 `0c8a7337`（未 push），任务 `11-17-dcomp-waitable-latency` 保持 `in_progress`，规范更新待定。
- 用户实机测试（120Hz+DRR/预设120、60Hz/预设240）日志分析与预测尖刺/卡顿调查：`11-17-dcomp-waitable-latency/research/field-test-20261117.md`（Codex 原文 `codex-predict-stutter-20261117.md`）。要点：A 无熔断/无错误；SyncInterval 0 下 waitable 约 1 ms 回补，几乎不节流；悬停帧不限速（约 900 Present/s）；尖刺最可能是“预设帧间隔阈值注入静止样本 + RTS 只取批末包 + Kalman 无长度上限”。
- 等待用户查看报告后，再决定 A/B 之间的修复（预测保护+日志、悬停限速、长帧诊断）与阶段 B 的完成方式；B 的 design/implement 尚未写。
- 用户追加（同日）：要求解释悬停帧不限速并调查光标层（大橡皮、多指橡皮卡顿）。记录在 `research/hover-cursor-layer-20261117.md`：悬停帧建议按预设帧率限速（小改动）；多指/大橡皮的主要放大因素是“全部光标旧新位置 + 全部落点并成单一脏矩形”，与第5步 §4.4 一致，候选方向为多矩形脏区 / 光标分层 / 精灵预热；附带用户要做的对照实测清单。
- 后续测试计划：用户将做 CPU/GPU 加压脚本下的高帧率测试（脚本待用户指示后再写）；熔断路径需故障注入开关才能触发（单纯加压不会熔断），需改代码，待定。用户正在复测 240 预设尖刺并做开关对照。本轮约定不改代码。
- 用户决策（同日）：鼠标光标大体采用方案 A（绘制时光标跟随画布帧率，两条鼠标路径一致；拒收时鼠标/笔/有光标的触摸仍按帧率跟随；性能页新增“绘制时鼠标光标不受画布帧率限制”默认关；悬停不限速）。实施前待确认 Q1（鼠标笔迹采样率下降）、Q3（拒收范围逐类）、Q5（选项实际影响整帧、命名与位置），详见 `research/hover-cursor-layer-20261117.md` §1.7。建议建独立小任务，A 收尾后、B 之前做。

## 恢复指引（2026-11-17，会话压缩前，以此为准）

- **当前状态**：阶段 A 已提交 `0c8a7337`（未 push，任务 `11-17-dcomp-waitable-latency` 保持 in_progress，规范更新待实机通过后再做）。预测尖刺在用户对照复测中未复现（用户推测是日志写入跟不上导致的卡顿），**不处理**。
- **下一项：阶段 A2 `11-17-cursor-frame-pacing`**（规划完成，status=planning，**等待用户批准后才开工**；未 `task.py start`）。文档：`prd.md`（需求、已确认决策、验收）、`design.md`（唤醒模型、窗口条件、帧尾节流分支、拒收触摸光标、设置项、测试、压力工具）、`implement.md`（步骤 0–10）。
  - 用户已确认：方案 A；鼠标笔迹采样率随之降为预设帧率可接受；悬停不限速；选项名“绘制时鼠标移动不受画布帧率限制”（默认关、附说明）；压力工具只施压、不做熔断、手动使用、不进自动测试、关窗即停。
  - 批准时需用户确认的一项：拒收范围按建议（①准入阻塞、②关闭多指显示光标；③手掌、④平移抑制、⑤选择模式、⑥兼容消息抑制保持现状）。
  - 开工第一步是 design 中的“开工核对项”，有矛盾先停下汇报。
- **之后**：阶段 B（`11-17-ulw-texture-pipeline`，只有 PRD；完成方式的取舍见 `11-17-dcomp-waitable-latency/research/field-test-20261117.md` §6，待用户决定），再 C。
- 本会话新增文档均未提交：`11-17-dcomp-waitable-latency/research/{field-test,codex-predict-stutter}-20261117.md`、`research/hover-cursor-layer-20261117.md`、本 handoff 修改、`11-17-cursor-frame-pacing/` 全部文件、父任务 `task.json`（children 新增 A2）。

## 进展（2026-11-17 夜，阶段 A2 实现完成，以此为准）

- A2 `11-17-cursor-frame-pacing` 已 `task.py start`（in_progress），代码全部完成，已提交 `696cc4f1`（未 push，任务保持 in_progress）。开工核对（Codex）发现 3 处与设计不符，用户“按建议继续”：D1 拒收触摸走新观察通道；D2 兼容鼠标 `CursorHeld()`；D3 手掌拒收的笔/鼠标光标也跟随。详见 A2 `design.md` §10 与 `research/startup-verification-20261117.md`。
- 实现要点：`ContactInputCoordinator::PublishPacedWake`（不打断帧尾等待）；窗口侧鼠标/笔按下期间纯移动改发节拍唤醒（新选项开启时鼠标恢复打断式）；帧尾新增 `active.empty() && cursorContactPacingActive()` 节流分支；拒收触摸登记表（带工作区，进入选择模式/切换工作区即退休）；`CursorSettings` bit44 + `Experimental.Inkeys3.Draw3.UnthrottledMouseMoveWhileDrawing`（默认关，`SyncDraw3State` 即时下发，性能页 waitable 之后，20 语言已译）；压力工具 `Scripts/Start-StressLoad.cmd/.ps1` + `Scripts/StressLoad/StressLoad.cpp`（GPU 时间戳计时、超 500 ms 降档、设备异常停止 GPU 部分）。
- 验证：Debug|ARM64 构建通过；`Inkeys.exe --draw3-control-fence-test` 通过（含新增窗口侧节拍唤醒检查）；新增 contact/mouse 单测；Codex 只读审阅无 P0/P1，4 条 P2 + 1 条 P3 已全部修复。帧尾分支没有整循环测试，需用户实机验证。
- 待办：用户实机测试（30/120 FPS 鼠标书写帧率、准入阻塞/关闭多指拒收光标、新选项开关、压力工具下高帧率书写）→ 通过后更新规范（`draw3-cursors.md`、`input-and-ink.md`、`settings-performance-and-drawing.md`）→ 经授权提交。之后阶段 B。

## 恢复指引（2026-11-18，阶段 A / A2 收尾，以此为准）

- **阶段 A、A2 已完成并归档**：代码 `0c8a7337`（A）、`696cc4f1`（A2）；用户 2026-11-18 实机验证通过（重启后 30 FPS 书写每秒约 20–30 帧，选项开关按预期）。规范已写入：
  - `draw3-integration.md`“Scenario: DComp waitable 帧延迟等待与熔断”；
  - `draw3-cursors.md`“Scenario: 绘制时光标跟随画布帧率”；
  - `input-and-ink.md`“Scenario: 节拍唤醒与被观察的拒收接触”；
  - `settings-performance-and-drawing.md` 两个新配置键与“渲染与兼容”节顺序。
- 子任务目录已移到 `.trellis/tasks/archive/2026-11/`（`11-17-dcomp-waitable-latency`、`11-17-cursor-frame-pacing`），其 `research/`（含 `field-test-20261117.md` §6 阶段 B 完成方式取舍）随之移动。
- 实测经验：画布帧率 `Drawing.FrameRate` 只在启动时生效，改档后必须重启再测；`[Draw3Diag][latency] acquired=` 每秒计数可直接当实际出帧率读。
- **下一项：阶段 B `11-17-ulw-texture-pipeline`**（只有 PRD，status=planning），由下一次对话开始。先读本文件“关键事实”、父任务 `research/presentation-pipeline-current.md` §4–§6 与归档的 `field-test-20261117.md` §6，再与用户确定完成方式后写 design/implement。之后阶段 C `11-17-perf-dashboard`（PRD 中对 A/B 的口径前提：等待耗时不计入工作耗时，A 已满足）。
- 分支 `refactoring/draw` 均未 push。

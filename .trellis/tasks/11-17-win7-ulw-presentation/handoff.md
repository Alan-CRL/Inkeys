# 第2步交接说明（2026-11-17，上下文压缩前）

## 一句话状态

第2步已拆为 A / B / C 三个子任务。**A（DComp waitable）规划完成，等待用户指示开工**；B、C 只有 PRD。仓库未改任何代码，Trellis 文档均未提交（用户未授权 commit）。

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

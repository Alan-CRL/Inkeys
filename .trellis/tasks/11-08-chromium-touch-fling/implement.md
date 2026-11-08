# Implementation Plan

## 约束（所有步骤适用）

- 最小 diff：只改完成本任务所需；不顺手重构、格式化、重命名；发现其他问题只报告。保持原文件编码与换行（`.cpp/.cppm/.h` 以仓库现状为准，`.trellis` 文档为 UTF-8 + CRLF），修改后检查 `git diff --stat` 无整文件差异。Git Bash 的 `sed -i` 会把 CRLF 改成 LF，需要用时务必还原。
- 关键逻辑加适量中文注释（边界、不直观的 Chromium 语义、符号约定）。
- 构建验证只做 `Debug|ARM64`，用 `InkeysRepo.sln` 的 `-t:` 目标，ARM64 host MSBuild：`C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe`（以本机实际为准，不硬编码版本）。同一 PowerShell 会话先 `Remove-Item Env:PATH -ErrorAction SilentlyContinue`（保留原值以便恢复）、`$env:MSBUILDDISABLENODEREUSE='1'`；不并发多个 MSBuild；构建超时至少 5 分钟；必须检查退出码。**构建与测试由主 Agent 执行**（Codex 沙箱内不能编译/运行 exe）。
- 不 commit、不 push；不执行 `trellis init/update`；不改 Trellis 自动化配置。
- 实施顺序：S1 → S2 → S3 → S4 → S5；阶段 2（S6）待阶段 1 通过后单独进行。
- Codex 沙箱不能编译：新引用的枚举、函数、类型须逐个 grep 确认存在；共享头里用 `(std::min)`/`(std::max)`；新增跨命名空间引用须按实际命名空间核对。

## 阶段 1

### S1 参考核对与数值黄金值
- [ ] 对照 `%LOCALAPPDATA%\Temp\inkeys-codex\chromium-src\` 下的源码核对 `research/chromium-fling-model.md`，把差异与 ppi/单位结论写回 `design.md §0/§1`。
- [ ] 在 `research/sim/`（新增 `chromium_fling.py`，纯标准库，`python -I` 运行）独立实现 LSQ2 估速、`CubicBezier::Solve`、`PhysicsBasedFlingCurve`（时长/距离/P1 修正/上限）、booster 逻辑，输出黄金值表 `results_chromium.txt`，供 S3 单测断言。

### S2 CPU 内核替换（`Draw3.CanvasNavigation.cppm/.cpp`）
- [ ] 删除旧摩擦、释放门限、手静止、`Slipping` 与相关常量/导出函数（先 grep 全仓引用确认）。
- [ ] 新增 `design.md §1` 的五个组件与门面；保留 `CanvasPanFrameInput/Output`、`Step/Stop/ClearAxis/Phase/Active/ContentVelocity/HeldContactCount` 接口形状；输出新增 `focus/span/pointerCount/scaleDelta`；曲线开关常量；"带历史样本加入"入口。
- [ ] 所有手感常量集中在 `.cppm` 顶部一块，附 Chromium 来源注释。
- [ ] 确认 `ApplyCanvasContentTranslation*`、`ScreenToCanvas`、tile 规划相关导出保持不变。

### S3 单元测试（`InkeysHeadlessTests/draw3_canvas_pan_tests.cpp` 等）
- [ ] 重写旧动力学契约测试；按 `design.md §10` 新增黄金值、手势、多指（§3 每行）、fling、booster、笔、缩放预留用例。
- [ ] 同步 `animation_tests.cpp` 的注册；`InkeysHeadlessTests.vcxproj` 仅在新增文件时改。
- [ ] 测试预期不得因"让测试通过"而放宽；预告会失败的断言要等真实输出再裁决。

### S4 控制器接入（`Draw3.DrawingController.cpp` 及必要的最小关联）
- [ ] 清理对已删常量/phase 的引用；笔 hover 仅 fling 时 `StopFling`、落笔 `Stop()`（`design.md §5`）；停止钩子重置 booster。
- [ ] 诊断输出（`LogCanvasPan`，仅 RTS 诊断模式）。
- [ ] 不改背景跟随、渲染、tile 规划、`TranslateViewport`、设备互斥；`HiddenWindowTest`/Host 里的相关引用按需最小同步。

### S5 文档与规范
- [ ] 改写 `.trellis/spec/native/runtime-and-rendering.md` 中惯性契约（摩擦耦合动力学相关条款）、`input-and-ink.md`/`draw3-integration.md`/`whiteboard.md` 中涉及的描述为 Chromium 模型；更新前置任务 `11-06-whiteboard-canvas-pan-inertia` 的 `design.md §2-3` 加指向本任务的说明（不删历史）。
- [ ] 更新 `implement.jsonl/check.jsonl`（如有新增 research）。

### 阶段 1 验收（主 Agent）
- [ ] `git diff --stat` 与逐文件审查（Minimal Diff、编码、EOL）。
- [ ] 构建 `InkeysHeadlessTests` 与 `Inkeys`（`Debug|ARM64`），检查退出码与首个错误。
- [ ] 运行 `InkeysHeadlessTests --no-window` 相关入口及完整 Headless 回归；运行 `--draw3-control-fence-test`。
- [ ] 独立复核：黄金值、AC1–AC6；必要时返回 Codex 修正；记录未覆盖项（真机手感）。

## 阶段 2（S6，阶段 1 通过后）

- [ ] 先出 design 补充并评审：RTS 批内全样本、有界队列、溢出与终态顺序、写锁竞争、时间戳来源。
- [ ] 实施 RTS 全样本与 `WaitForFrameDeadline` 提前唤醒；补测试；单独提交。

## 回滚点

- 曲线开关回 Legacy；阶段 1 / 阶段 2 分别提交可独立回退；回退阶段 1 即恢复 `708cb6bc` 的摩擦耦合内核。

## 第二轮交接状态（2026-11-08）

第一轮真实结果由主 Agent 提供：InkeysHeadlessTests 与 Inkeys 的 Debug|ARM64 构建均 exit 0，`--no-window --canvas-pan-only` 与完整 `--no-window` 均 PASS。control-fence 两处每页视角断言失败；canvas-pan-feel 的 constant-100 有32处稳定delta断言失败；两项探针在本轮修订后尚待复跑。

- A：首次 Provider scroll 当前少于3指时逐指径向扣15后平均；当前≥3指不扣、二维全零不派发；同步CPU测试、生产探针和文档。
- B：保留 controller inactive 立即结束、不补发；补总位移及尾差测试，纯曲线终点黄金值不变。
- C：采纳拓扑边界之间合并连续Move、严格QPC排序优先；补同帧Move→Down→Move和Move→Up→Move用例。
- D：阶段1仅首指history seed；活动多指等待阶段2同步格式，保持当前拒绝行为。
- E：静止拓扑contentDelta=0且视口不变，focus按质心重算；速度用绝对容差<0.01 DIP/s（LSQ float舍入），不加归零阈值。
- C4244：仅为distance计算的double→float转换添加显式转换，保留原算术与舍入。
- constant-100：失败断言在Drag循环内，Release仅在Drag结束后调用；属于首次scroll/slop边界，非释放或fling start_time回拨缺陷。稳定delta只比较连续两个完整的后续跟手区间，阈值2%+0.5px不变；首次scroll另按径向扣15精确校验。

本轮仅做静态复核，未编译/执行应用、Headless或Python；命令运行器Access denied/子进程EPERM，用Node文件接口保留BOM/CRLF并比较本轮字节基线，真实Git diff与本轮构建/探针复跑由主 Agent 执行。Python脚本仅将A/B待决说明改为已裁决说明，所有数值公式和纯曲线黄金值保持不变。任务保持in_progress，未commit/push/归档，未进入S6。

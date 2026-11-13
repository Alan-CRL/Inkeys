> 2026-11-13 当前收尾：用户确认本机惯性合适，并授权清理、验证、归档和提交；本基础任务与后续拖动任务一并收尾。当前结果见 [后续任务收尾验证](../11-08-chromium-touch-fling/cleanup-validation-2026-11-13.md)；下文旧未勾选项、in_progress及不提交限制为历史记录。尺度适配另开下一任务，PTP/其它平台与背景性能证据边界按收尾记录保留，不push。

> 2026-11-13 状态核对：本任务的基础平移、每页视角与背景跟随已实现；动力学、设备准入/hover 与输入调度以 [后续任务状态审查](../11-08-chromium-touch-fling/research/status-review-2026-11-13.md) 为当前依据。S6/S7 未勾选清单与 S8 已验证记录并存，不能据此认为基础功能均未实现，也不能将旧摩擦耦合验收直接用于当前代码。真机设备/纹理/性能仍未全部验收，任务保留 `in_progress`；触控板及新输入后端按后续任务 [未来规划](../11-08-chromium-touch-fling/research/input-backend-future-plan-2026-11-13.md) 延期，不在本轮实现。

# Implementation Plan

约束：保持原编码与行尾；不 commit/push；不启动 GUI（真机手感与视觉验收由用户另行执行）。

构建与测试（用户 2026-11-06 明确指示，本任务覆盖 `build-and-compatibility.md` 中"必须完整 Solution"的默认要求）：
- 只构建 `Debug|ARM64`（本机原生架构，用 ARM64 host 的 MSBuild），不构建 Release，不必每轮全量构建。迭代时通过 `InkeysRepo.sln` 的 `-t:<项目>` 只构建需要的项目（S0/S1 用 `InkeysHeadlessTests`，S2–S5 用 `Inkeys` 及其依赖），保持规范"不单独构建 vcxproj"的原意；整个解决方案只在最后按你的意愿决定是否构建一次。参考耗时：`InkeysHeadlessTests` 项目约 1 分钟、增量 8–15 秒，完整解决方案约 5 分钟。
- 构建/测试一律前台直接执行，不加起始 `sleep`/轮询；只有明显很长的命令才后台运行并等待完成通知。优先只跑相关测试入口，不必每次跑完整 Headless（约 80 秒）。
- 不并发多个 MSBuild（历史日志出现过 `C1902` PDB 管理器冲突）。

每阶段末尾自检后再进入下一阶段，各阶段回退点见末尾。

上下文说明：`implement.jsonl`/`check.jsonl` 中的大文件会在注入时被截断，实施前直接阅读 `runtime-and-rendering.md` 的 "Scenario: Free Canvas Navigation And Predictive Recovery"（约 L848-931）、`cpu-gpu-contracts.md` 的平移/快照节，以及三份 research 文档全文。

## S0 基线与测试挂接

- [x] 把 `Draw3.CanvasNavigation.cpp/.cppm` 加入 `InkeysHeadlessTests.vcxproj`；新建 `draw3_canvas_pan_tests.cpp`，在 `animation_tests.cpp` 补前置声明（约 :71-79）和 `main` 调用（约 :1877），按现有注册风格。
- [x] 为现有估速/惯性写特征化测试（二指错开抬起 120 Hz 下 P(>2×)≈5%、同向叠加 glide2/single∈[1.0,1.05]、停手幻影），作为替换前基线；S6 随旧代码一起删除。

## S1 纯 CPU 动力学（不改变产品行为）

- [x] `.cppm` 顶部集中常量：`kVMax=12000`、自由摩擦 `(0.30, 1000)`、手摩擦 `(0.06, 6000)`、停止阈值 20、最小释放 250、释放门限 30/70 ms、估速窗口 100 ms/≥3 样本/≥20 ms 跨度/2 ms 合并/40 ms 断口、`kHandStationaryDelay=30 ms`、静止位移 4 DIP。
- [x] 实现 `CanvasPanVelocityTracker`、手速聚合、闭式摩擦积分、`CanvasPanDynamics` 状态机（Idle/Coasting/Slipping/Locked）、释放门限与封顶、笔 hover/落笔、限位清轴；纯函数、无窗口、noexcept 风格与现有模块一致。
- [x] 单测：`research/inertia-root-cause.md` §4.7 的 1–17 与 24；新增耦合用例（惯性中静止按住比自由滑行更快停下并被抓住；同向加速被带到手速且速度连续；反向减速快于按住；Slipping 中抬起保持速度；轻点 <30 ms 不影响；多指加减不跳变；连续同向甩动封顶 12000）。
- [x] 自检：`git diff --check`；Headless 纯 CPU 用例通过。

## S2 门禁与输入路由

- [x] `Draw3.Bridge.h` 增加 `MoveModePansCanvas`；删除 `kCanvasNavigationProductIntegrationEnabled` 及其使用点（`Draw3.WindowControl.cppm:30`、控制器 `:12153`）。
- [x] `WindowController` 通过 `Detail::MouseSelectionModeState` 从 Selection 与已确认工作区直接派生门禁（`SetCanvasPanWorkspace`）；`MouseInputState` 与 `HandleCompatibilityMouse`（`Draw3.WindowControl.cpp:494-541`）按 `selectionMode && !panMode` 隔离；更新 `draw3_mouse_input_tests.cpp`。
- [x] `processCommand` 分流（design §1）：`registerPanContact`、设备类别互斥、Mouse-left 不触发 `interruptNavigationForPenOrMouse`；Pen/MouseRight 继续丢弃。
- [x] 统一 `stopCanvasNavigation(reason)` 接入离开移动模式、`SetWorkspace`、翻页、加载、最小化、Resize；修正 Selection 释放 1 ms 计时器的条件（design §3）。
- [x] 更新 `RunDraw3ControlFenceProductionProbe` 的白板 Selection 期望与新增 Touch/MouseLeft 平移用例。

## S3 控制器接入

- [x] 重写 `updateCanvasNavigation` 为 design §3 流程；DIP↔像素换算；视口应用块复用；`contentVelocity` 驱动 tile 预测；保留笔 mailbox 语义（hover 手摩擦、落笔急停）。
- [x] 保留显式 RTS diagnostics 下的 `LogCanvasPan`（含 phase、`v_c`、`v_h`、release age），正常模式零输出，供真机标定。
- [x] 生产探针/回归：Move 模式下触摸与鼠标拖动 → 视口按 1:1 变化；Slipping→Locked；释放→惯性；模式离开即停；翻页后无残余位移。

## S4 每页视角

- [x] 增加 `whiteboardViewports` 侧表及写入/恢复点（design §5）；确保不改 `revision`、不触发保存。
- [x] 探针：平移→翻页→返回恢复；`EvictWhiteboardPage` 后冷页重载恢复；新页 (0,0)。

## S5 背景纹理迁入 Draw3

- [x] 渲染：纹理 under pass、under blend state、scissor raster、primitive 生成与上传（R1）；参数结构每帧传入；`appliedPatternKey` 与主题命令后的 `forceFullPresent`；工作区/空白门控；排查空页捷径。
- [x] `Whiteboard.cpp:130`/`:689`：Freeze 只画纯色且仅 RGB 变化才失效；清理 `DrawTheme` 无调用方（保留 `DrawThemePattern` 给预览）。
- [x] 离屏 WARP 验证：相位 0 与 D2D 参考逐像素对比、alpha 不累积、相位按公式随视口位移、桌面/PPT 不绘制、不进 L2/快照；Freeze 纯色断言。
- [x] 若 4K 帧耗时（`RuntimeMetricsCostStage::Composite`）不达标，评估 R2（新 HLSL shape ≥23 + Win7 guarded 重生成）并回到设计评审，不擅自扩大范围。

## S5′ 背景纹理改由 Freeze 窗口跟随（默认归属，2026-11-06 用户纠正后新增）

依据 `research/freeze-pattern-follow-design.md`。S5 的 Draw3 shader pass 保留为后备，由一个 constexpr 归属开关选择（默认 Freeze）。

- [x] 归属开关 `kWhiteboardPatternOwner`（默认 Freeze）放入 `Draw3.WhiteboardModel.h`；Draw3 侧 `syncWhiteboardPattern` 单点门控，Freeze 归属下 S5 路径惰性但仍编译、`--draw3-whiteboard-pattern-test` 仍通过。
- [x] 视口通道：Whiteboard 模块持有 seqlock「最新视口」，Draw3 绘制线程通过新增运行时回调写入（含页/代次，页切换、冷页恢复、主题变化同步发布），写入后 `RenderPipeline::Request(WhiteboardFreeze)`；`RenderBackground` 在 UI3 线程直接读取。
- [x] Freeze 更新路径：恢复改动前 `DrawTheme` 并带相位的变体；相位变化只做 scene damage（`backgroundScene.Invalidate()`），不调用 `backgroundState.Invalidate()`/`CancelBackgroundCandidate()`；回执 ack 后同一回调内立刻画下一帧；提交中不得新增 scene damage；每次 ack 都 `Prepare`。RGB/纹理类型变化沿用原失效逻辑；开关为 Draw3 时保持 S5 的纯色/RGB-only 失效。
- [x] 诊断（沿用 `INKEYS_SETTING_ENTRY_PROFILE` 模式，仅显式诊断输出）：M1 Freeze 纹理每秒更新次数；M2 相对 Draw3 视口的滞后（帧、ms、px）。
- [x] 测试：seqlock 与相位数学的 Headless 单测；隐藏 WARP 测试验证带偏移的纹理像素与参照一致（扩展 `RunHiddenBackgroundTests` / `--page-control-hidden-test`）；两种开关取值下相关入口都通过。
- [x] 未验证项如实记录：4K/100% DPI 单次更新成本、UI3 线程饥饿与功耗、>3000 DIP/s 的视觉不同步（需真机）。

## S6 清理与规范

- [x] 删除旧 180 ms 双指/`PanCandidate`/`InertiaBrakeRequested`/`cancelTouchDrawingForPan` 与旧 `CanvasPanMotionState` 估速/候选代码，更新 `OnTouchDown` 调用方探针（现为 `TouchContactRegistry`）；绘制模式 Touch 行为保持不变。`retireEndedTouchBeforeDown` 与按 key 的 Touch 身份登记（重复/零 key 的 Down 只跟踪到终态）因行为等价作为最小 shim 保留。
- [ ] 通过 `trellis-update-spec` 改写 `runtime-and-rendering.md`（Free Canvas Navigation 全节）、`whiteboard.md`、`rendering-and-ui.md:1758`、`cpu-gpu-contracts.md`；提取"摩擦耦合""手速估计""under 非幂等"等可执行契约。

## S7 验证

- [ ] `git diff --check`。
- [ ] 同一 PowerShell 会话先 `Remove-Item Env:PATH -ErrorAction SilentlyContinue`、`$env:MSBUILDDISABLENODEREUSE=1`，用 ARM64 host MSBuild 对 `InkeysRepo.sln` 以 `-p:Configuration=Debug -p:Platform=ARM64 -m` 构建 `Inkeys` 与 `InkeysHeadlessTests`（`-t:`），只做 Debug|ARM64；是否再做一次完整解决方案构建由用户决定。
- [ ] `InkeysHeadlessTests` 以 `--no-window` 运行相关入口；运行生产探针 `--draw3-control-fence-test` 及相关 Draw3/Whiteboard 无窗口入口；最后跑一次完整 Headless 作为回归。
- [ ] `trellis-check` 独立审查（跨层：输入→导航→渲染→持久化）；修复后复核。
- [ ] 记录未验证项：真机触摸/鼠标手感与常量标定、DComp 与 ULW 视觉、4K 帧耗时、Win7 WARP、RTS 鼠标拖出窗口/contactId。

## 回退点

- S1：模块独立，不接线即无产品影响。
- S2/S3：`MoveModePansCanvas` 恒为 false 即整体关闭平移（恢复为丢弃）。
- S4：侧表独立。
- S5：渲染 pass 与 Freeze 改动同提交，回退时一并恢复。
- 阶段 S5 与 S1–S4 彼此独立，必要时可拆为独立任务交付。

## S8 离屏窗口测试与手感边界验证（2026-11-08 用户追加，当前任务收尾后执行）

分工：Codex 编写/修正测试与产品代码，主 Agent 构建并运行、把输出回交 Codex（Codex 沙箱内无法运行可执行文件，不使用 danger-full-access；Codex 的 computer-use 不可用）。优先离屏/隐藏窗口，不弹可见窗口；若必须有窗口，注意锁屏窗口且暂时无法手动关闭，运行要带超时并确保进程可被终止。

- [x] 离屏手感矩阵（`RunCanvasPanFeelProductionProbe` / `--draw3-canvas-pan-feel-test`，走真实适配层+动力学+控制器视口应用，帧率 30/60/120/240 Hz，DPI 100/150/200%（另含非等比）、帧/子帧交付，61+ 场景，主 Agent 本机末行 `[Draw3PanFeel] PASS failures=0 rows=2112`）：轻点、长按、静止抖动、各速度甩动（含 12000 DIP/s 上限）、连续同向甩动叠加、惯性中静止按住、反向、惯性中加速同向、多指加减/错开抬起、停手后抬起、掉帧（dt 尖峰）、QPC 异常、视口 ±1048576 边界清轴；指标：每帧速度跳变上限、位移连续性、停止时间、总滑行距离相对闭式解、停止后残余位移、抓住后与手 1:1 误差。
- [x] 中断类边界：惯性中笔 hover/落笔、离开移动模式、翻页、切工作区、加载、最小化、Resize、触摸/鼠标设备交接，均零残余位移、无遗留活动帧/1 ms 计时器。
- [x] 隐藏窗口层：真实 `WindowController`/Win32 兜底路径注入鼠标消息，验证首个 Down、拖出窗口、捕获丢失；白板隐藏窗口下 Freeze 纹理跟随（`--page-control-hidden-test` 扩展）；两种归属开关取值各验证一次。
- [x] S8 发现问题已修正并由主 Agent 在本机 Debug|ARM64、WARP、离屏重跑；规范已补录时间戳/排序/同帧交接/Pen 仲裁/门禁重置/无效触点活动性规则与已知限制。
- 真机待验（S3.5）：真实触摸/笔/鼠标手感与摩擦、释放等常量标定，离屏矩阵不替代设备验收。
- 真机待验（窗口层）：真实 RTS 拖出窗口与笔提升兼容鼠标行为；`CheckHiddenControllerMousePan` 的 RTS/Draw3 归属 SKIP 不算实测通过。
- 真机待验（纹理）：DWM/显示器滞后、4K/DPI/多屏与 UI3 争用功耗；屏外 M1 22–35 新像素帧/s、47–60 回调/s、M2 最大 3–4 样本仅作软件侧证据，约 1500–3000 DIP/s 以上可见滞后属待真机判断。

## S3.5 手感标定（用户真机参与）

整体手感是本任务的首要验收维度，单独成阶段：常量集中在 `.cppm` 顶部，诊断日志（显式 RTS diagnostics 下）输出 phase、`v_c`、`v_h`、release age；每轮由用户在真机试触摸/鼠标/连续甩动/按住/反向，反馈后只调常量（自由摩擦、手摩擦、30 ms 轻点阈值、释放门限 30/70 ms、`kVMax`、估速窗口），重新构建再试。S5 完成后再做一轮"纹理跟随与墨迹是否错位/发飘"的手感确认。

## 时间评估（工作量级，非承诺；2026-11-06 按用户反馈下调）

按 Agent 实际工作时长估算：构建只做 Debug|ARM64 的目标项目（1 分钟级，增量更短），测试直接前台运行、无起始 sleep。时间主要花在读写代码本身，而不是等待构建。

| 阶段 | 估时 | 主要不确定性 |
|---|---|---|
| S0 基线与挂接 | 15–30 min | 测试注册风格 |
| S1 纯 CPU 动力学 | 45–90 min | 状态机边界与测试数量 |
| S2 门禁与输入路由 | 1–2 h | 控制器体量、探针更新面 |
| S3 控制器接入 | 1.5–3 h | 18k 行控制器、旧 FSM 旁路、DPI/计时器 |
| S3.5 手感标定 | 每轮约 15–30 min（我这侧），预计 2–4 轮 | 取决于用户真机反馈节奏 |
| S4 每页视角 | 20–40 min | 冷页加载路径 |
| S5 纹理迁入 | 2.5–5 h（退到 R2 方案再 +2–4 h） | 4K 帧耗时、under 非幂等、空页捷径 |
| S6 清理与规范 | 1–1.5 h | 旧探针与 spec 改写面 |
| S7 验证 | 30–60 min | 修复轮数 |

串行合计约 8–15 h（不含手感标定的往返）；S5 与 S1–S4 无依赖，但共享控制器与渲染调用点，并行需严格划分文件所有权，可再省约 2–4 h。日历时间主要取决于你参与手感标定的间隔。

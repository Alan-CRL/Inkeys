> 2026-11-12 当前执行项（用户已选择暂停 PTP）：
> 1. [x] 在 WindowControl 的 DM 初始化边界执行 quarantine，禁止同 RTS HWND 的 COM/Activate；保留其它设备及下游数值测试。
> 2. [x] 完整 ARM64 Solution、control-fence、pan-feel、Headless pan/contact及完整 --no-window均exit0；diff/编码通过。真机 Mouse/Touch/Pen/PenTail 拖动→回绘制仍由用户验收，PTP 明确不再验收为本轮可用。
> 3. [x] 同步 canvas-pan/input-and-ink/cross-layer 规范及验证记录；本轮已获 commit 授权，不 push/archive。
>
> 当前续接为真机回归修复：用户报告所有接触设备拖不动、PTP仅偶尔可用，修复范围与验证见 [multi-input-regression-scope.md](multi-input-regression-scope.md)。前轮无窗口PASS不代表本轮实际输入正常；按已批准功能直接修复，任务继续in_progress。

# 当前阶段执行计划：多设备漫游与惯性设置

状态：用户已批准最终方案并要求实施。已授权当前清单实现；未授权 GUI/commit/push，task 继续 in_progress，不归档。执行前按 implement.jsonl → prd.md → design.md → 本文件读取，旧 S1–S6 不作为当前待办。

## 顺序与交付

1. [x] 触控板稳定接线验证：依据 research/multi-input-roaming.md 的稳定 API/primary implementation，核对 PT_TOUCHPAD hit-test → DM viewport/contact → owner manual Update → 累计变换/status/epoch 的真实生产接线和坐标方向；不建立另一 DComp 树，不调用 GUI。以可替换数值输出/假回调测试生命周期，OS 真实消息验证另列未完成。
2. [x] 策略/配置：增加五类惯性策略与六个 NoUpload bool，默认/序列化/启动发布/Host 同代快照/只有变化唤醒；加入缺项、非法类型、冻结 copy、ReadMini、Reset、子偏好保留测试。保持旧惯性常量。
3. [x] 笔/笔尾：扩展 Selection 准入和 Down 来源锁存，修正 suppressed 判定与持续 mailbox 自我 Stop；拖动模式关闭全部 hover brake。新 Down、Touch 持有互斥、Up 后持续 hover、源切换全部通过真实生产 route/probe。
4. [x] 触控板整合：只支持双指，DM 仅发布数值，由唯一 Draw3 owner 应用；系统 INERTIA 独占该来源并驱动同一导航活动帧/高精度计时器，开关同步、取消/页/模式/尺寸/失败/重入、旧 epoch 与重复 wheel/pointer 不双算。不可用时保留原功能，不伪造普通 wheel 生命周期。
5. [x] 设置与国际化：绘制 → 通用增加 ToggleRow+InlineCheckboxes，五项顺序 Mouse/Touchpad/Touch/Pen/PenTail；父关闭收起保留偏好；接既有异步写盘。补二十语言与生成产物。
6. [x] 回归：适用无窗口专项、完整 Headless、生产探针、i18n check、编码/EOL/最小 diff，trellis-check 后按事实更新 spec。用户未授权前不 commit/push/finish-work/archive。

## 最小文件范围与责任

- 输入/策略：Draw3.DrawingController.cpp/.cppm、Draw3.CanvasNavigation.cpp/.cppm；WindowControl.cpp/.cppm 用于 DM owner 生命周期及 Pen hover/触控板调度。优先现有文件或小型私有 helper，只有实际需要新增编译单元时登记项目项。
- 配置发布：Other.Config.cppm、IdtMain.cpp、Draw3.Host.h/.cpp。启动和设置一致，owner 不重复发 wake。
- 设置：Setting.cpp、必要的小型 Setting.Pages.h/.cpp helper；不改共享控件或橡皮逻辑。
- 文案：Inkeys/src/i18n/*.jsonc、生成 IdtI18nKeys.g.h 和 scripts/i18n.zh-CN.snapshot.jsonc。
- 测试：现有 draw3_canvas_pan_tests.cpp、draw3_contact_tests.cpp（若适用）、config_metadata_tests.cpp、setting_design_tests.cpp、DrawingController 生产 control-fence/pan-feel probes。避免仅测镜像 helper。

## 必须守住的验证

- 惯性关：最后 Move/Up 的几何与终态不丢、无 residual/boost 复活；热关闭/重开、五类切换与多源互斥。
- Pen/尾：真实倒置元数据准入，不借橡皮开关；合法 Pen 不被 suppressed 假阳性；持续接触不自我 Stop。所有来源 coast 遇 hover 不停，真实 Down 取消旧运动。
- 节拍：保留真实 Mouse publisher burst/持续流与状态边沿回归；加 Pen hover 连续流及触控板累计更新，普通变化不提前结束活动 deadline，控制/终态仍及时唤醒。
- Touchpad：RUNNING→INERTIA→READY、重入/重接触、同帧取消/页切换/迟到回调、总/子开关停止系统 tail、无重复运动、初始化失败/旧系统缺 API、像素/DIP/方向与累计基线。
- 设置：真实 ImGui 回放，单击一次事件、稳定 ID、master 子值保留/收起禁交互、窄宽/多 DPI/主题/语言；真实 Config 默认/持久化/非法类型/Reset/NoUpload。

## 命令与构建约束

- 先复用已有脚本和命令，不新增构建系统/依赖。修改后用 scripts/i18n.ps1 sync/check（填全翻译后 check）。
- 通过 vswhere 动态定位 ARM64 原生 MSBuild，同一 PowerShell invocation 先 Remove-Item Env:PATH -ErrorAction SilentlyContinue 和 MSBUILDDISABLENODEREUSE=1；构建 InkeysRepo.sln Debug|ARM64，/m:1 /nr:false，超时至少 5 分钟，正常预算 30 分钟。不能单构建 Inkeys.vcxproj。
- InkeysHeadlessTests.exe --no-window --canvas-pan-only；--no-window --setting-general-only；完整 --no-window；产品 --config-boundary-test、--draw3-control-fence-test、--draw3-canvas-pan-feel-test；本阶段若动诊断传递，再补 --draw3-diagnostic-output-test。命令均走现有 GUI 前提前返回路径，使用无窗口进程方式。
- 检查退出码和首个有意义错误，不改项目/SDK 来掩盖环境故障；签名/SSH失败不绕过。完整 Headless 允许至少 10 分钟。
- git diff --check --ignore-submodules、逐文件差异与编码/BOM/CRLF 检查。新文件也用相邻原格式。构建/探针证据写本任务验证文档，不把模拟数据或 Present 返回称光学/真机验收。

## 当前规划检查

本阶段已按用户批准实现并通过最终完整ARM64构建、全部专项及完整Headless无窗口回归，源/产物哈希冻结。实际结果见multi-input-validation.md，独立审查见research/multi-input-review.md；真实PTP/笔硬件/RTS共存/光学未验。任务继续in_progress，不自动GUI/commit/push/归档；屏幕尺度及历史Chromium仅作后续参考。

---

# 历史阶段记录（仅参考，不作为当前实施清单）

> 当前续接进入屏幕尺度阶段方案评审，见 [scale-stage-plan.md](scale-stage-plan.md)。4b4edb91是已确认的手感基线；本轮未修改生产代码，原implementation-scope.md保留为已完成阶段。

> 本轮实现与无窗口验证已完成，结果和环境问题处理见 [validation.md](validation.md)。用户真机手感及后续屏幕尺度仍待进行；未提交、未归档。

> 当前实施以 [implementation-scope.md](implementation-scope.md) 为准：用户已批准本轮修复鼠标/触摸手感并接通诊断，屏幕尺度延后；以下阶段1及先前调查记录保留为历史。

> 2026-11-08 续接调查：阶段1已提交为 `7b213d90`；用户反馈手感退化。本轮仅调查与规划，新的候选目标、证据、实施顺序及验收见 [repair-plan.md](repair-plan.md)。下文保留阶段1历史合同；原S6及新修复均须先评审，不据 task 的 in_progress 状态自动实施。

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

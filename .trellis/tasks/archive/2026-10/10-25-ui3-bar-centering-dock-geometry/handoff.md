# UI3 底栏几何修复 — 居中阶段交接

> 2026-10-26 当前执行授权：用户要求提交当前改动，完成后允许一轮原生竖直Logo拖动，肉眼观察后结束本轮。先完成commit，再编译本次版本、等待用户准备并重放；不运行自动单测、不push、不归档。测试结果不能由下方历史PASS或静态审查代替。

> 2026-10-26 最新：用户人工确认吸附脏区外残影已修复。子任务已实现独立顶边目标、a7dc错误抓点约束清理与形变中再次Down直移基准修正，完成静态交叉审查；按要求未编译/运行自动测试或GUI，等用户人工验证，脏区内跳变尚未闭环。详见 `../10-25-ui3-bottom-dock-transition-clipping/research/top-edge-fix-review-2026-10-26.md`；以下旧PASS/停止点不代表当前修复通过。

## -3. 2026-10-26 用户实测回归与闪烁调查（当前最高优先级）

- 用户实测确认 `a7dc3a08` 引入上端回归且闪烁仍在。当前 anchored Docked 映射会为了抓点和底端反推顶边，违反用户明确的“顶边不动、底边独立果冻”设计；现有测试又错误要求 `actualGrab == pointer`，没有该顶边不变量。
- 捕获时 `window_h/pptSrc` 确实会改变，backing target 在同 case 内不变；提示峰值只在 `maximumIndicatorScale>0` 后才加入 predicted viewport，是旧的呈现触发点。Down 时预留完整手势包络可行，但不能用扩窗掩盖内容错误。
- 最新静态调查为 `../10-25-ui3-bottom-dock-transition-clipping/research/regression-review-2026-10-26.md`。父/吸附任务 `software=historical_tests_passed_symptom_unresolved`、`gui=authorized_pending_live_reproduction`，继续保持 `in_progress`，不得按下方旧 PASS 宣称完成。
- 另一路 `research/live-reproduction-2026-10-26.md` 尚未确认原始剧烈闪烁复现，并明确要求观察到相同症状前禁止产品修复。复现门解除后的候选顺序才是：恢复顶边不变量及测试、独立实施 Down 完整包络/容量预留，再验证整个手势的窗口尺寸/source 稳定及真实视觉效果。

## -2. 2026-10-26 修复与全量验证（历史记录，已被用户实测否决）

- 已按后续审阅完成手动捕获有效输入播种、真实归一化抓点、直移本地＋全局屏障及底边高度恢复；保留此前脱离、吸收、容量、居中和 Freeze 修复。不要重复实现或覆盖这些修改。
- 最新证据为 `../10-25-ui3-bottom-dock-transition-clipping/research/review-fix-validation-2026-10-26.md`：19 组吸附离屏、居中场景、相关回归、完整 ARM64 Solution 和 full-scope `trellis-check` 均通过；1196 行样本中最大抓点误差 `2.27374e-13 px`，50 次合法直移、19 次屏障后放行，提前直移为 0。
- 主会话已完成完整 `InkeysHeadlessTests --no-window`：退出 0，359.523 秒，末尾 `PASS animation correctness`。此前“全量未完成”记录以此最终结果为准。
- `.trellis/spec/native-desktop/rendering-and-ui.md` 与 `../10-25-ui3-bottom-dock-transition-clipping/research/break-loop-2026-10-26.md` 已更新。父/吸附任务 `software=passed`，但真实 GUI/Office/WPS/ULW-DWM、多屏和主观手感仍未验证，继续保持 `in_progress`，不归档。
- 下一 agent 只需考虑经用户授权的真实视觉验收或新的实际缺陷，不要只重跑旧测试后再次宣称 GUI 已完成。

## -1. 2026-10-26 后续审阅（历史缺口，已由上方最新修复覆盖）

- 接管提交 `1ba08698` 的吸附验收已被最新审阅撤回：手动捕获旧底边 seed 可用历史实机数值复现错误，真实局部抓点未修，离屏绕过 `SetWindowPos` 且缺位置断言。
- 下一 agent 必须先读 `.trellis/tasks/archive/2026-10/10-25-ui3-bottom-dock-transition-clipping/research/review-2026-10-26.md`。父/吸附任务 `software=review_failed`，当前任务指针为 none；不要只依据下方 integration PASS 继续宣称完成。
- 本轮用户选择仅静态审阅，未启动 GUI；后续针对性修复与回归方案见审阅文档。

## 0. 2026-10-26 接力结果（历史记录，吸附 PASS 不等于验收）

- 原 6 文件居中修改已复核，无产品 DPI/zoom 策略改动；完整 no-window 已补齐并在最终测试产物再次退出 0（337.813s）。
- 用户追加 Freeze 隐藏退场已修复：保留 List 身份，通用前驱锚点和既有批次，隐藏成员在可见成员下层；两种 A2 顺序、合并/分离、左右展开及 100%/150% 共 1248 帧通过。
- 吸附三窄修复及父任务组合已实施；两个真实离屏入口、既有 eraser 离屏、完整 Debug|ARM64 Solution、独立 CSV/格式检查均通过。最后 tracked 范围是 10 个文件（含规范），无 commit/push/archive。
- 19 组吸附组合、674 成功帧；动态居中 6 组/72 阶段/2370 成功样本。最终报告：`research/final-integration-validation.md`。
- 当前父/子任务均为 in_progress，当前任务指针已恢复为父任务；剩余真实 GUI、Office/WPS、ULW/DWM、多屏、最小化恢复和手感验收，已向用户请求授权，未授权前不运行。
- 后续先读最终报告及新的 task notes，再决定 GUI 接力；不要把下方旧“吸附未实施/全量未完成”当作最新状态，也不要重复实现或覆盖本轮修改。


## 1. 停止点与任务状态

- 日期：2026-10-25；分支：`bugfix/cut`；调查基线 HEAD：`fdda877d1d65dae877ee46cd081f36940eea23f6`。
- 用户要求立即停止在动态宽度居中收尾边界，并由其他 agent 接力。未启动吸附实现，未执行 git commit 或 archive。
- `10-25-ui3-centered-bar-dynamic-centering`：保持 `in_progress`。生产窄修复、真实离屏回归和 `trellis-check` 已完成；完整 `--no-window` 与真实视觉验收仍未完成，不能视为整个子任务已验收完成。
- `10-25-ui3-bottom-dock-transition-clipping`：保持 `planning`，规划完整，尚未实施。
- 父任务：保持 `planning`，等待两个子任务完成后的组合集成审查。
- 本次会话结束时清除当前任务指针，不改变以上状态。下一 agent 应从本文件和吸附子任务继续，不要重开居中实现。

## 2. 当前未提交修改

保留以下 6 个 tracked 文件的修改；不要回退或覆盖：

- `Inkeys/IdtMain.cpp`：在产品窗口初始化前接入 `--ui3-workspace-centering-test`。
- `Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h`：`ShouldDeriveBarBottomDockCenteredRoot` 增加 `workspacePlacementActive`，仅把 display 门禁细化为 `!displayTransitionActive || workspacePlacementActive`，其余 Centered/Stable/Expanded、无拖动/弹簧条件不变。
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm`：导出真实离屏场景入口。
- `Inkeys/Inkeys/UI/Bar/Bar.MultiTarget.Test.cpp`：新增真实 RenderLoop/离屏 ULW sink 场景与严格断言；harness 关闭时仅保留返回 2 的 stub。
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp`：实际根反推和 viewport 根范围预测共用上述纯门禁，均使用现有 `whiteboardDockAnimationActive`；未改 Leave、Y/宽度曲线或吸附状态机。
- `InkeysHeadlessTests/bar_bottom_dock_tests.cpp`：新增 workspace ownership、普通 display 负向用例、连续宽度、左右展开、描边、负原点及无效几何覆盖。

临时 headless 选择器已还原，最终 tracked diff 不包含它们。`TestResults/ui3-workspace-centering/` 保存基线、修复后样本和日志。

另有未相关未跟踪目录 `.trellis/tasks/09-06-setting-winui3-design-rebuild/`，不要纳入本任务或修改。

## 3. 已确认的实施与质量证据

详细记录：`../10-25-ui3-centered-bar-dynamic-centering/research/implementation-validation.md`。

### 修复前真实复现

`./Build/ARM64/Debug/Inkeys.exe --ui3-workspace-centering-test` 在旧门禁上退出 1；`right-100` 的真实 RenderLoop 中间成功帧出现：

- Desktop→PPT 最大偏心 `105.3 px`；
- Desktop→Whiteboard、PPT→Whiteboard 最大偏心 `20.7936 px`；
- Whiteboard→PPT 最大偏心 `101.27568 px`；
- 快速反向也有非零误差。

原始样本：`TestResults/ui3-workspace-centering/baseline-before-fix.csv`。独立 Leave 在旧实现中已为 0 误差，证明 Leave 不能与 Enter/Switch 混同。

### 修复后与 trellis-check

- 同一入口退出 0；6 组、72 阶段、2370 个成功样本，最大中心误差 `1.13687e-13 px`。
- 覆盖左右展开、96/144 DPI、合成负 monitor 原点、Enter/Switch/Leave 和快速反向。
- source/target、局部 dirty、Centered tuple、根 X 变化的 full-replacement 错误均为 0；每组 present 失败注入后成功 serial 保持并完成整窗恢复。
- `trellis-check` 进一步删除左展准备阶段非法 source 例外、禁止 malformed ULW 被后续成功掩盖，并要求每阶段存在中间成功帧及正确 Centered tuple；收紧后仍全部通过。
- 相关 headless 选择集均退出 0：bottom dock、display transition、PPT session、dirty region、window geometry。临时选择器已还原。
- 最终完整 `InkeysRepo.sln Debug|ARM64` 构建成功；仅有既有第三方 `C4267` 警告，不声称 0 warning。
- `git diff --check` 通过，原 BOM/CRLF 保持。

最终样本/日志：`TestResults/ui3-workspace-centering/samples.csv`、`check-offscreen.log`。旧基线未包含后来追加的失败注入，不能宣称与最终 fixture 逐字节相同；核心 Enter/Switch 复现与修复后对应阶段一致。

## 4. 明确未完成/未验证事项

1. **完整 `InkeysHeadlessTests --no-window` 未完成。** 实施与审查中的运行达到 300 秒上限或误用旧产物，均不能记为通过。接力 agent 应在最终当前产物上用至少 600 秒期限重跑，记录真实退出码和末尾 `PASS animation correctness`。
2. Whiteboard 最小化/恢复没有无 HWND 的生产状态入口，本轮未覆盖，未新增生产旁路。
3. 未启动真实产品 GUI、Office/WPS 或真实 ULW/DWM 验收；真实跨屏/原屏消失、普通居中拖动和吸附附近视觉/命中仍待授权后验证。
4. 合成负原点和 DPI 不等价于真实显示矩阵；存在真实显示服务时负原点 fixture 会明确 `SKIP`，不得把 SKIP 当覆盖。
5. Release stub/早退只做了静态审查，未另跑 Release。
6. 未做居中与吸附的组合集成审查；吸附代码尚未实施。

## 5. 下一 agent 接力顺序

1. 先阅读本文件、居中 `research/implementation-validation.md`、吸附子任务的 `prd.md` / `design.md` / `implement.md` / `research/dock-transition-clipping-investigation.md` 及其 `implement.jsonl`。保留当前 6 个 tracked 修改，不重做居中。
2. 先核对 `git status` 和 `git diff`，再运行：
   ```bash
   ./Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window
   ./Build/ARM64/Debug/Inkeys.exe --ui3-workspace-centering-test
   ```
   完整无窗口回归至少给予 600 秒。若失败，先区分居中回归、既有失败和环境问题，不能把失败直接归入吸附。
3. 需要重新构建时使用：
   ```bash
   "C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/arm64/MSBuild.exe" InkeysRepo.sln -p:Configuration=Debug -p:Platform=ARM64
   ```
   Git Bash 必须用 `-p:`；完整 Solution 构建至少给予 5 分钟。
4. 复核当前计划后启动吸附子任务：
   ```bash
   python ./.trellis/scripts/task.py start 10-25-ui3-bottom-dock-transition-clipping
   ```
   若平台需要 `TRELLIS_CONTEXT_ID`，使用当前接力会话自己的 ID，不要复制本次会话 ID。父任务不应代替子任务先行启动。
5. 吸附实施继续测试先行，按 `10-25-ui3-bottom-dock-transition-clipping/implement.md` 分三个独立单元：脱离首帧成功形状 seed、释放吸收完整快照重基准、完整预测与最终 capacity/source 包含。普通捕获和水平远端已有保护不得重写。
6. 真实局部抓点、dirty/glyph 或 ULW/DWM 只在逐帧证据指向时进入对应分支；不得原样恢复历史上被回退的大补丁。
7. 与居中共享的 `Bar.RenderLoop.cpp`、`Bar.BottomDock.h`、`Bar.Main.cppm` 和 offscreen harness 必须串行修改。完成后执行父任务组合集成和全范围 `trellis-check`。

## 6. 接力质量门

- 新回归必须先证明旧实现失败，再证明修复后通过；不得复制理想公式或人工保证前提后自证。
- `--ui3-workspace-centering-test` 已存在并通过；`--ui3-bottom-dock-transition-test` 仍是后续拟新增入口，当前不能运行或声称通过。
- 完整构建、完整 `--no-window`、两个真实离屏场景及各自质量检查是后续最低自动化门槛。
- GUI/Office/WPS/真实 ULW/DWM 未获授权或未执行时，必须继续标注未验证；不得据 headless 全绿宣称用户视觉症状全部消失。
- 不提交、不归档、不修改 spec，除非接力用户/工作流在对应阶段明确要求。任何额外产品范围变化先回到 PRD/设计审查。

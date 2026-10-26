# UI3 底栏居中与吸附帧几何修复 — 集成执行计划

> 2026-10-26 当前执行授权：用户要求提交当前改动，完成后允许一轮原生竖直Logo拖动，肉眼观察后结束本轮。先完成commit，再编译本次版本、等待用户准备并重放；不运行自动单测、不push、不归档。测试结果不能由下方历史PASS或静态审查代替。

> 2026-10-26 最新实测：用户发现 `a7dc3a08` 引入顶边上移回归且闪烁仍在，父/吸附任务 `software=historical_tests_passed_symptom_unresolved`、`gui=authorized_pending_live_reproduction`。必须先读 `handoff.md`、`../10-25-ui3-bottom-dock-transition-clipping/research/regression-review-2026-10-26.md` 和 `live-reproduction-2026-10-26.md`。原生重放尚未确认原始剧烈闪烁，复现前禁止产品修复；解除该门后，才按“恢复顶边不动及正确验收、独立 Down 完整手势包络/容量预留”的顺序实施。下文为集成计划，当前进度以最新调查为准。

> 历史审阅：`research/review-2026-10-26.md` 记录了接管提交 `1ba08698` 当时的手动捕获、真实抓点和直移缺口，后续修复依据与结果见上述最新验证。

## 有序实施清单

### 1. 规划与复现基线

- [ ] 完成并审查两个子任务研究文档，确保直接根因、推断和未运行验证项分开。
- [ ] 将用户症状映射到可观测断言：逐帧联合中心、上一成功像素连续性、两轴 serial、完整提示包络、真实 HWND/dirty。
- [ ] 在现有测试项目中加入最小回归覆盖；修复前确认对应场景失败，避免重复已有理想 helper 测试却仍漏掉生产集成路径。
- [ ] 保留本轮当前 HEAD 完整构建与无窗口基线结果；后续产品变更后重新完整构建，再比较新增与既有回归结果。

### 2. 居中子任务

- [ ] 按 `../10-25-ui3-centered-bar-dynamic-centering/implement.md` 实施，先用真实 RenderLoop + 离屏 ULW sink 复现 Enter/Switch 中间成功帧偏心。
- [ ] 实际根反推与 viewport 根范围预测共用窄门禁；Y 保持场景动画，X 按当前联合几何反推，不重写最终 Leave。
- [ ] 确认每帧在主栏/按钮动画之后、下游 Popup/提示几何之前完成联合中心反推和继承；Free、拖动、弹簧和普通显示切换门禁无回归。
- [ ] 分别验证 Enter、Switch、Leave、快速反向和最小化/恢复，形成独立回退点。

### 3. 吸附与提示子任务

- [ ] 按 `../10-25-ui3-bottom-dock-transition-clipping/implement.md` 先用真实 `Seek` + RenderLoop 建立失败反例，再分别实施三个独立修复单元：脱离首帧成功形状 seed、释放吸收完整快照重基准、完整预测与最终 capacity/source 包含。
- [ ] 普通捕获和水平远端已有保护不得重写；进入吸附若仍异常，必须记录新的实际序列后再判因。
- [ ] 验证脱离首个成功候选不提前积分、吸收前后屏幕坐标不变、横纵 tuple 与 translation 同事务提交。
- [ ] 验证提示完整视觉包络同时进入 damage、candidate viewport、实际 target/source 与真实 HWND；越界不得钳 source 后提交。
- [ ] 真实局部抓点、dirty/glyph 或 ULW/DWM 只在逐帧证据指向时进入对应分支，形成独立回退点。

### 4. 父任务集成审查

- [ ] 审查 `Bar.RenderLoop.cpp`、`Bar.BottomDock.h`、`Bar.Main.cppm` 及测试的交叉修改，确认没有双写根位置、重复补偿、半轴 serial 或窗口/内容错位。
- [ ] 运行动态居中 + 吸附切换 + 提示组合场景，而非只分别验证两个子任务终态。
- [ ] 核对动画关闭、左右展开、Centered/Free、release handoff、失败呈现和重试路径。
- [ ] 更新受影响的规范/任务研究结论，确保规范描述与最终实现一致。

## 验证命令

后续产品代码变更及拟新增离屏场景入口后的标准本机验证：

```bash
"C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/arm64/MSBuild.exe" InkeysRepo.sln -p:Configuration=Debug -p:Platform=ARM64
./Build/ARM64/Debug/Inkeys.exe --ui3-workspace-centering-test
./Build/ARM64/Debug/Inkeys.exe --ui3-bottom-dock-transition-test
./Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window
```

- 完整 Solution 构建超时至少 5 分钟；不得用单独项目构建代替。
- `--ui3-workspace-centering-test` 已实现并通过居中离屏验证；`--ui3-bottom-dock-transition-test` 仍是后续拟新增入口，必须复用现有 harness/真实生产路径，不能提前声称存在或通过。
- 离屏结果不能覆盖真实 HWND/Office/DWM 的部分必须单独记录；检查实际失败，不以进程结束或日志末尾 PASS 代替目标场景断言。

## 真实视觉验收矩阵（需另行授权 GUI）

- Desktop→PPT→Desktop、Desktop→Whiteboard→Desktop、PPT↔Whiteboard；Enter、Switch、最终 Leave 和快速反向分别记录。
- Centered/Free、左/右展开、主栏变宽与 Y 移动并行、快速反向和动画关闭。
- 底栏与居中底栏进入/退出吸附，横纵同时跨越阈值，捕获前距离超过普通 24 DIP 保护范围，靠近居中位置的水平跳变。
- “底栏模式”与“底栏模式 · 居中”首次出现、双向文案切换、退场和失败重试；同时开启 damage 红/绿框与真实 HWND 蓝框。
- 既有支持的 DPI、非零 monitor 原点和任务栏配置；记录实际环境，不把未运行配置宣称为通过。

逐帧记录应包含主按钮/主栏联合屏幕中心、主栏底边、frame/presented translation、serial、弹簧初值、提示包络、candidate viewport、实际 target/source、dirty 和真实 HWND。视觉验收未执行时，任务不得声称真实症状已完全验证。

## 风险与回退点

1. **回归测试落地后**：确认测试确实命中原缺陷；若只是复制公式、人工保证包含或测试终态，先修测试。
2. **居中子任务完成后**：可独立回退场景根位置门禁及配套测试；Leave 独立样本失败时返回调查，不套用 Enter 修复。
3. **吸附各单元完成后**：脱离 seed、吸收重基准、容量包含分别独立回退；真实抓点大改不得混入这些窄修复。
4. **最终集成前**：任何真实 GUI 未验证项、失败重试或 dirty/viewport 异常都必须明确保留为阻塞或经用户确认的延期项。

## 完成前检查

- [ ] 父/子 PRD、设计和研究文件相互链接且没有未决阻塞问题。
- [ ] `implement.jsonl` 与 `check.jsonl` 包含真实规范和研究条目，并通过 Trellis validate。
- [ ] 后续实施获得用户对最新规划的明确批准后，才启动拥有下一交付物的子任务。
- [ ] 最终代码变更经子任务检查和父任务全范围集成检查；未经验证不得归档为视觉修复完成。

## 2026-10-26 实际执行状态

- [x] 接力差异、所有权、DPI/范围与原始证据复核。
- [x] 本任务软件实现、能够先红后绿的回归及共享文件串行集成。
- [x] 当前 Debug|ARM64 Solution、完整 no-window、两个真实离屏入口及共用隐藏锚点的既有像素回归。
- [x] 全范围 trellis-check、CSV/编码/换行与最小变更范围复核、规范和上下文更新。
- [ ] 授权后真实 GUI/Office/WPS/ULW/DWM、多屏、白板最小化恢复与主观手感验收。

原规划清单保留历史；本节及父任务最终验证报告表示当前执行状态。未收到 GUI 授权，不 commit/push/archive。

# 验证记录：激活抬笔与落笔屏蔽

日期：2026-11-07；分支 bugfix/hand-erase，基线 babdecf6（未提交）。

## 实施分工

- 实现：Codex `gpt-6.1-sol`，reasoning effort high，`service_tier=default`，`--sandbox workspace-write`；改动 `Draw3.DrawingController.cpp`（生产逻辑 + 27 项生产 probe 用例）与 `zh-CN.jsonc` 文案。
- 主 Agent（Claude）：独立审查生产逻辑（范围判定、激活帧冻结与单次收尾、`parallelOnly` 过滤、续接顺序与拒收、并行关闭路径未变）；修复测试中不存在的枚举 `InputDeviceType::Mouse` → `MouseLeft`（2 处，编译错误 C2065/C2838）；补全 19 种语言译文；构建与测试。

## 结果

| 项目 | 结果 |
| --- | --- |
| `Scripts/i18n.ps1 sync/check`（pwsh 7；Windows PowerShell 5.1 因无 BOM 脚本解析失败，属环境） | 20 语 770/770，PASS |
| InkeysRepo.sln Debug\|ARM64，ARM64 原生 MSBuild | 首次因上述枚举错误失败；修复后 exit 0 |
| `Inkeys.exe --draw3-palm-eraser-test` | exit 0，`[Draw3Palm] PASS`（含新增用例） |
| `InkeysHeadlessTests.exe --no-window --palm-eraser-only` | 152 项 0 失败 |
| `InkeysHeadlessTests.exe --no-window` 全量 | exit 0，stderr 空，PASS animation correctness |

## 未验证

- 大屏多触点实机（边擦边写下的真实手掌与邻近手指）、Win7 运行；GUI 未启动。
- 矩形手掌屏蔽范围（D4）：已在 custom 子任务 R6 实现并测试，见 `11-07-palm-eraser-phase2-custom/validation.md`。

## PR 前审阅与修复（2026-11-07）

以下为本次文档收尾依据的主 Agent 核实结论与独立运行记录；文档收尾未重新执行构建或测试。

- 审阅方式：Codex `gpt-6.1-sol`（High，Fast，只读）三路独立审阅：手掌逻辑 / 渲染存储 / 设置文档；主 Agent 核实，修复交 Codex `gpt-6.1-sol`（High，Fast，workspace-write）执行两轮。
- 已修复（提交 `3a055c34`）：
  1. `Draw3.DrawingController.cpp` 的 `PromotePalmRuntime`：普通活动橡皮（原 tool 已是 Eraser 且有擦除点）转手掌时，保留该笔 Down 锁存的形状，避免手掌所选形状覆盖整笔 eraserShape、改变旧擦除点固化/重放形状；手掌形状与定尺、光标、屏蔽范围同步为该形状。其他原工具（笔 / 荧光笔 / 激光 / 图形）仍用手掌所选形状。`--draw3-palm-eraser-test` 相关断言已改为 Eraser 保留原形状、Pen/Laser 覆盖。
  2. 两处新增的邻近 Touch 落笔拒收分支（`RejectPalmNeighborTouch`）在调用前保存 MetricKey，并补 `Invalidate(InitRejected)` 退休已登记的 metrics 记录；无低成本夹具，未新增专门测试。
  3. spec `palm-eraser.md` 中“不使用速度尺寸控制器”限定为动态模式，并补充上述形状保留规则。
- 已知未修项按用户决定仅记录、不修复：blocking 的两项低概率竞态（中）见 [blocking validation](../11-07-palm-eraser-phase2-blocking/validation.md)“PR 前审阅：已知未修项”；光标纹样显隐使用烘焙尺寸（低）见 [shapes validation](../11-07-palm-eraser-phase2-shapes/validation.md) 同名节。任务完成不代表这些问题已修复，也不代表未测场景已验证。

最终验证结果（主 Agent 独立运行）：

| 项目 | 结果 |
| --- | --- |
| `InkeysRepo.sln` Debug\|ARM64 | exit 0 |
| `--draw3-palm-eraser-test` | PASS |
| `--config-boundary-test` | PASS |
| `--draw3-renderer-upload-test` | PASS failures=0 |
| `InkeysHeadlessTests --no-window` 全量 | exit 0，无失败 |
| `--palm-eraser-only`（首轮修复后） | cases=2053 failures=0 |
| `Scripts/i18n.ps1 check` | 通过 |
| `git diff --check` | 通过 |

仍未验证：真实大屏多触点、GUI 观感、真实 GPU 光标、Win7 运行；此前记录的待母语复核项仍保留。

## PR 前审阅：已知未修项

按用户决定仅记录、不修复。以下两项均为中等影响、低概率竞态，未作实机验证。

- 竞态 A：激活帧的抬笔到帧末 FinishActivatedPalmNeighbors（Draw3.DrawingController.cpp 约 18013 行）才执行，Down 准入/续接却在帧中。范围内处于等待续接的普通 Touch 笔，可能先被同批一个落在范围外、但满足既有续接条件的新 Down 接走；palmNeighborBlocked 只依据新 Down 位置，接回后位置更新到范围外，帧末检查看不到，从而逃过抬笔。触发条件：新 Down 紧贴旧笔抬起点，又落在手掌外扩 max(15%,4px) 之外。
- 竞态 B：已激活且未移动的手掌 Up 与邻近新 Touch Down 在两帧间发生，且 Down.qpc < 手掌 Up.qpc；帧循环先消费手掌 Up 并置 ended，PalmNeighborBlocked 跳过 ended 手掌，随后处理的新 Down 不被拒收，可书写并移出范围。触发还需手掌无续接路径。
- 影响：手掌附近多/少一笔墨迹，不丢数据、不崩溃。
- 未修原因：需新增跨帧状态，改动落在最复杂的 Down 准入/续接逻辑，且无实机/GUI 验证。
- 建议处理时机：先在大屏多人书写实机观察，出现误写再修；修复优先级 B 先于 A。

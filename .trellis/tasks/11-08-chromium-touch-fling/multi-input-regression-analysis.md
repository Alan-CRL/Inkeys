# Bug Analysis: RTS/DM 同 HWND 输入委托冲突（2026-11-12 当前结论）

## 1. Root Cause Category

- **B Cross-Layer Contract / E Implicit Assumption**：把DM viewport的INPUT/EXPLICITHITTEST误认为足以保护同HWND上的RTS producer。真实冲突位于manager Activate的HWND输入委托层，早于SetContact、Running和CanvasPan consumer。
- **D Test Coverage Gap**：此前fake viewport和直接Bridge/Coordinator注入绕过系统委托，无法观察Activate失败及RTS后续异常。

## 2. Why Fixes Failed

1. flags/显式hit-test只调整DM viewport消息策略，没有消除同HWND DelegateInput竞争。
2. SetContact S_OK改真实Running修正了持有语义，但RTS已在DM初始化边界受损，下游收不到接触。
3. 独立DM HWND初始化虽成功，真实PT_TOUCHPAD在不注册主窗口时没有到达；注册后跨HWND SetContact仍返回0x80070490，故不能把换窗当修复。

## 3. Prevention Mechanisms

| 优先级 | 机制 | 本轮处理 | 状态 |
| --- | --- | --- | --- |
| P0 | 从结构上避免冲突 | quarantine在任何COM/Activate前停用DM owner，保留RTS | 已实现 |
| P0 | 防止误重新启用 | 既有生产frontend probe新增quarantine断言 | 已实现，control-fence通过 |
| P0 | 真实系统边界证据 | 同HWND正反序/分窗/重试及合成双指PTP对照，保留原始日志 | 已完成独立探针 |
| P1 | 规范与交接 | canvas-pan/input-and-ink/cross-layer及任务记录明确当前停用和证据边界 | 已更新 |
| P1 | 真机闭环 | 用户验证Mouse/Touch/Pen/PenTail拖动→绘制；PTP本轮明确不可用 | 待用户 |

## 4. Systematic Expansion

未来任何系统手势引擎接入RTS窗口都必须先检查HWND级委托所有权，不只检查消息flags。替代方案须同时证明初始化、真实Down/Move/Up、DM退休和RTS后续绘制；只证明COM成功或下游数值正确不够。当前用户选择暂停PTP，不新建另一输入后端，也不借未评估的RTS重启恢复。

## 5. Knowledge Capture

实际证据见research/dm-rts-coexistence-probe.md；当前范围见multi-input-regression-scope.md。规范已同步；仓库不存在src/templates/markdown/spec模板树，无模板同步。按用户既有授权边界不commit/push/archive。以下为前轮分析，仅保留历史，已被上述系统证据取代。

---

# Bug Analysis: DM前端抢占风险与未识别手势伪held（历史）

## 1. Root Cause Category

- **B Cross-Layer Contract / E Implicit Assumption**：将MANUALUPDATE误当成输入隔离，漏掉INPUT/EXPLICITHITTEST；应用只过滤显式SetContact不能约束系统运行中默认命中。SetContact的成功只表示关联，却直接发布Running，混淆请求成功与系统确认状态。
- **C Change Propagation Failure**：引入系统viewport/manual更新后没有接入IDirectManipulationInteractionEventHandler BEGIN/END，泵只能借伪Running启动；等待识别与实际持有没有独立状态。
- **D Test Coverage Gap**：前轮真实Coordinator/bridge consumer探针确实执行了，但直接PublishHiddenTestTouchpadNavigation绕过真实viewport初始化/消息分流/回调注册，因此所有后端测试PASS仍漏前端合同缺口。附件有接触型导航为零的证据，但从seq328中途开始，不能证明所有物理尝试均在片段，也不能把rts-batch排Pen的日志当笔无RTS证明。

## 2. Why Fixes Failed

1. 前轮集中验证释放、来源历史和COM停止责任，未验证系统输入引擎如何影响自有producer之前的消息交付。
2. 用SetContact成功伪造Running以维持更新，看起来解决调度，实际可能让未识别手势长期占用互斥；默认DM自动命中又可能吞掉其它输入。
3. 初始化/重复READY可能早于真正BEGIN。直接沿旧伪held清理策略退休candidate，又会提前丢弃有效新手势；本轮把raw状态许可纳入共享生产routine。

## 3. Prevention Mechanisms

| 优先级 | 机制 | 本轮处理 | 状态 |
| --- | --- | --- | --- |
| P0 | 前端系统输入合同 | 固定setup包含manual update/输入可见/显式命中，只关联PTP，整DM hit-test应用处理 | 已实现并编译 |
| P0 | 调度与持有分离 | S_OK不Running；Interaction BEGIN/END→NeedsUpdate，确认Status→held，Content先到缓存 | 已实现并专项通过 |
| P0 | 共享实际入口回归 | setup/association/status/interaction/hit-test routine由Owner和fake边界共用，四类实际Coordinator三事件+旧policy负对照 | 本轮专项通过，完整Headless原样复核通过（首轮未改动调度时序失败保留） |
| P1 | 初始化与重入 | 初始READY不退休未确认candidate，确认READY/END/失败/epoch不复活；固定配置不在contact处理中重配 | 已实现并专项通过 |
| P1 | 诊断定位 | gate下hit-test来源、关联结果、Interaction/Status、source拒收原因，不只输出准入成功 | 已实现 |
| P1 | 证据分级 | 明确fake OS策略模型、实际生产函数调用、编译/无窗口测试与真机RTS/PTP不同证据等级 | 已写验证与review |

## 4. Systematic Expansion

只核对同一Owner初始化/association/Content/Status/interaction/取消和Controller活动调度；未扩展UI、默认、摩擦、RTS后端或尺度。InputMode和UpdateMode不能混淆；没有为普通mouse/wheel/keyboard添加DM ProcessInput路径。目标仍Win10/11 PTP，其余设备保留原输入路径。

可执行预防已写入canvas-pan.md及input-and-ink.md；cross-layer-thinking-guide新增系统手势引擎对上游producer的触发项。不创建另一套系统手势框架或可调参数。仓库没有src/templates/markdown/spec模板树，模板同步不适用。

## 5. Knowledge Capture

- 已更新 .trellis/spec/native-desktop/canvas-pan.md：setup字段/NeedsUpdate/真实回调/状态许可/验证矩阵/错误示例。
- 已更新 .trellis/spec/native-desktop/input-and-ink.md 和 .trellis/spec/guides/cross-layer-thinking-guide.md：前端所有权与证据边界。
- 研究证据：research/multi-input-regression-log-analysis.md；独立审查：research/multi-input-regression-review.md；实际执行：multi-input-regression-validation.md。
- 当前确认的是源码/API合同缺口及本轮修复；没有GUI或真实OS/RTS输入注入，不能宣称已在所有设备上复现并验收。没有新增commit授权，按仓库规则不提交。

## Primary sources

- [Microsoft viewport options](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/ne-directmanipulation-directmanipulation_viewport_options)
- [Microsoft SetContact](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationviewport-setcontact)
- [Microsoft OnInteraction](https://learn.microsoft.com/en-us/windows/win32/api/directmanipulation/nf-directmanipulation-idirectmanipulationinteractioneventhandler-oninteraction)
- [Chromium handler](https://chromium.googlesource.com/chromium/src/+/HEAD/content/browser/renderer_host/direct_manipulation_event_handler_win.cc)

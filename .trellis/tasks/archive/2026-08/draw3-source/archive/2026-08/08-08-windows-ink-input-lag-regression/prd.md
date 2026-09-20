# Windows Ink 调查收尾与接触态应用光标开关

> 2026-08-09 最终裁决：实体对照否定了“最新提交引入 Pen 特有严重拖手回归”的判断。下方原诊断要求只作为历史调查记录保留，不再代表当前生产实现。

## Final Goal

撤回 `418338a`、`2520c2c` 及后续未提交 cadence/probe 对生产和测试代码的诊断扩展，恢复 `949752a` 的运行行为；保留独立确认的 writer-latch ownership 修复，并提供一个等待 Inkeys 设置层接入的默认关闭运行时开关，使普通 Pen/Highlighter 在绘制时显示与 Hover 相同的应用光标。

## Final Requirements

- FR1：保留 `949752a` 的 Move 先于 cursor 发布和 Contact cursor 不逐包唤醒，不再宣称它已被证明是延迟根因修复。
- FR2：移除 `--rts-trace-output`、Release RTS session trace、Modeler-to-Present trace、raw/tail probe、ordered pending ring、相关指标及测试 hook；保留 `949752a` 已有的基础 Debug diagnostics/metrics。
- FR3：移除专用 A/B 可执行文件、日志、probe 构建和 detached worktree；不得修改用户的 `Vcpkg/` 或其他 TestResults。
- FR4：`StrokeModelConfiguration` 提供默认 `false` 的绘制时应用光标设置；本仓库不增加按钮、热键或持久化。
- FR5：`DrawingController` 提供线程安全 setter/getter；值变化时只发布一次 control wake，Contact 包仍只覆盖最新 cursor mailbox。
- FR6：开关开启后，仅普通 Pen/Highlighter 的 Pen Contact 使用与 Hover 完全相同的应用内 transient cursor appearance；关闭时保持原行为。
- FR7：系统光标隐藏策略、Mouse、Touch、Laser、Eraser、倒转笔尾、Unknown authority fallback 和 Up 后清除行为保持不变。
- FR8：槽位跨 generation 复用不得重置 `writerLatch_` ownership；只有实际持锁者可以释放。
- FR9：完成 ARM64 Debug/Release 全解决方案构建、两套自动测试、静态引用检查、BOM/CRLF 和 `git diff --check`。

## Final Acceptance Criteria

- [x] 生产/测试源码中不再存在已撤回的诊断、cadence 或 probe 符号。
- [x] 配置默认关闭；Pen 与 Highlighter 开启后 Contact cursor 与对应 Hover visual 完全一致。
- [x] Mouse、Touch、Laser、Eraser、倒转笔尾及系统 cursor 矩阵测试无变化。
- [x] writer-latch 最小修复保留，ordered ring 与确定性诊断 hook 不保留。
- [x] 诊断专用 TestResults、临时任务和 detached worktree 已清除，`Vcpkg/` 未改动。
- [x] Debug/Release ARM64 构建与测试、编码和静态质量门通过。
- [x] 用户已完成最终人工核验并确认任务可以结束。

## Final Out Of Scope

- Inkeys 产品设置页、JSON 持久化及 Draw3 集成接线。
- 恢复约 270 Hz 绘制、合入 cadence B 或改变 120 Hz deadline。
- 宣称问题 2/3 已修复；本轮只记录未复现。

## Historical Diagnostic Plan (Superseded)

## Goal

本任务不修复任何生产行为，只建立低扰动、可关联的输入诊断 session。第一阶段已确认真实 Pen 输入能够稳定到达 DrawingController / Stroke Modeler；第二阶段回答一个成功进入 Modeler 的 point 何时形成 geometry、触发 render，并提交给 DXGI `Present1`；第三阶段用显式启动的 transient 双标记，把应用内 raw-to-visible 增量与实体笔尖到应用 raw marker 的剩余延迟分开观察。

## Background And Evidence Boundary

- 当前有两个相关但尚未证明同源的现象：Windows Ink Pen 相对 7 月 29 日基线拖尾并偶发追赶跳跃，以及最新版本偶发落笔后完全没有建立笔画。
- `f76a35a` 是可检查的 7 月 29 日附近基线；用户保存的原始构建仍是体验基线。
- `8f1ee5a`、`415ee5e`、`74c33fc`、`949752a` 都只是待验证候选影响点，不能被描述为已证明根因。
- `949752a` 已改变 cursor wake / Move 发布顺序，但真机结果没有证明其根因判断成立；本阶段以该 commit 为源码基底，只加诊断。
- 实体 Pen 与 Mouse 输入必须由用户手工完成。禁止 `SendInput`、GUI 自动化、synthetic pointer 或自动控制鼠标。

## Requirements

- R1：`--rts-trace` 在可执行文件旁创建唯一的 `InputDebugLogs/input-debug-<pid>-<session-id>.log`；`--rts-trace-output <path>` 可指定现有目录中的文件。
- R2：日志包含明确 BEGIN/END、session ID、QPC/frequency、PID、基底 commit、构建时间、配置和架构；一次启动对应一个文件。
- R3：每个 `StylusDown` 明确记录参数、decoder 初始状态、ensure 尝试/结果、lifecycle/decoder/binding generation、property count、解码坐标/压力、PublishDown 尝试/结果和最终 reason。
- R4：每个 `Packets` 早退明确区分 invalid arguments、state gate busy、binding/decoder 缺失、generation/context/property mismatch、decode failure 与 PublishMove failure；成功只采样并保留总计数。
- R5：失败 reason 即使 timeline 覆盖或诊断写锁争用也保留总数及首末 callback sequence/QPC；contact reason 不依赖可能截断的长 sequence 文本。
- R6：DrawingController 记录 Down dequeue/initialize、snapshot read/filter、Modeler update/deferred Up 与 recycle，使用 tcid/cid、contact generation、snapshot sequence 和 producer/consumer QPC 关联。
- R7：设备标签区分 Pen、MouseLeft、MouseRight、Touch；decoder/state gate 尚不能证明设备类型时必须记录 Unknown，不能猜成 Pen。
- R8：RTS callback 启用时只写 fixed-size 内存结构和 lock-free/nonblocking 原子；禁止 callback 内文件/console IO、heap allocation、等待、阻塞 mutex、COM 查询和新 wake。
- R9：Release ARM64 支持显式开启 diagnostics；未开启时 RTS 修改路径只做一次轻量启用检查，DrawingController 不读取额外 record/snapshot 字段或执行诊断查找。
- R10：每个实际活动 contact frame 具有轻量 `frameSeq`，并记录 frame start、连续 frame interval、设备、contact identity/generation 和 Active/Terminal phase；完全空闲循环不写逐帧日志。
- R11：每个活动 contact frame 记录最新已读取 snapshot 的 QPC/sequence/坐标/压力，以及 frame start 和 snapshot read 两个时点的 input age，单位统一为微秒。
- R12：Modeler 成功后记录实际 `modeledResults`、`realPoints`、`predictedPoints`、`l0DrawPoints`、L1 committed index、prediction endpoint、drawable/changed geometry；每笔前三帧必须保留完整记录。
- R13：记录 dirty rect、full/forced present、stroke/cursor 分类、render begin/end/duration，并识别 geometry changed 但没有 render 的 frame。
- R14：在真实 `Present1` 调用位置记录 begin/end QPC、HRESULT、SyncInterval、flags、dirty rect、连续 begin/end interval；明确返回时间只代表 CPU submission 返回，不是 input-to-photon。
- R15：只观察现有 120 Hz deadline：记录 requested budget、wait begin/end、目标 deadline、实际等待和 overshoot；禁止修改等待、线程优先级、Present 参数或接入 frame-latency waitable object。
- R16：退出时输出按 contact generation 和设备的轻量 aggregate；固定容量 POD/原子记录，热路径无文件/console IO、heap allocation、阻塞 mutex、COM query、新 wake 或 wait。
- R17：`--pen-latency-probe` 默认关闭；关闭时不得显示标记、增加 cursor dirty 或改变 Present cadence。
- R18：probe 的 raw 标记只读取同步 RTS callback 发布的 Pen contact 样本，不得把 `WM_POINTERUPDATE` 共用 mailbox 冒充 RTS-only 来源。
- R19：RTS-only mailbox 继续使用固定原子一致快照；contact Move 只覆盖最新值，不新增 render/control wake、等待、分配、IO、COM/D3D 或模型计算。
- R20：同一帧在普通 Pen contact 上绘制两枚 transient 标记：raw RTS point 与该帧实际用于 L0 可见几何的 `l0DrawPoints.back()`；不得用 `predictedPoints.back()` 冒充最终可见端点。
- R21：双标记复用现有 `DrawTransientDrawingCursor` 和 cursor dirty/清旧区机制，位于墨迹合成之后，不进入 L0/L1/L2、contact payload、模型或 metrics。
- R22：诊断说明必须明确：raw-to-visible 屏幕间距可隔离应用内增量；实体笔尖到 raw marker 仍混合 digitizer/driver/Windows Ink、120Hz 帧量化、DComp/DWM、scanout 和相机误差，不能仅凭该标记宣布 MPP2.0 硬件根因。
- R23：`--pen-latency-probe` 开启且当前为普通 Pen 工具时，只对 Pen authority（或 Unknown authority + 有效 Pen 样本）保留窗口 `IDC_ARROW`，供实体笔尖、系统光标、RTS raw marker 与 L0 visible-tail marker 同屏比较；Mouse、Touch、无 Pen 的 Unknown 以及 Eraser/Laser 继续使用既有隐藏矩阵。系统光标同样经过 Windows 输入与桌面合成，不得解释为硬件真值。

## Acceptance Criteria

- [x] Down 和 Packets 所有目标早退路径具有独立 `RtsPacketResult`。
- [x] 正常 Packets 采样，失败事件尽量进入固定 timeline，全部 reason 始终进入 aggregate。
- [x] Pen/Mouse/Touch/Unknown 与 DrawingController correlation 字段写入同一时间线。
- [x] session 文件具有唯一默认路径和完整 BEGIN/END/build 标记。
- [x] 非 GUI 测试覆盖 session 边界、reason 文本、成功采样、Unknown 标签、timeline overwrite 以及 aggregate 保留。
- [x] reviewer auxiliary 修复后的完整解决方案 `Debug|ARM64` 构建和 Debug ARM64 测试通过。
- [x] reviewer auxiliary 修复后的完整解决方案 `Release|ARM64` 构建和 Release ARM64 测试通过。
- [x] 静态审计确认 callback 热路径没有新增 IO、分配、阻塞、COM、wake、wait 或生产行为变化。
- [x] 第一轮真实 Pen/Mouse 日志已按 Down -> Packets -> Drawing -> Modeler 重建，并确认 decoder/binding/state gate、Publish、Drawing contact 初始化、snapshot decode 和 Modeler update 均无失败证据。
- [ ] 第二阶段日志可按 `frameSeq` 重建 latest snapshot -> Modeler output -> geometry/dirty -> render -> Present1 -> deadline wait。
- [ ] 日志可直接识别约 16.6ms 或更长的 frame/present gap、render/Present spike、活动 contact 未 Present 和 Down 到首次 geometry/render/Present 的停顿层。
- [x] `--pen-latency-probe` 关闭时行为与基线一致；开启且为普通 Pen 时按 R23 保留系统光标，raw 标记严格来自 RTS-only contact mailbox，并与实际 L0 可见尾端同时绘制。
- [x] 自动测试覆盖 RTS/Pointer 来源隔离、contact/hover/clear、probe 开关和 raw/tail visual 选择；完整 Debug/Release ARM64 构建与测试通过。

## Out Of Scope

- 修改 state gate、decoder lifecycle、cursor wake cadence、frame deadline、coordinator、contact queue/seqlock 或线程优先级。
- 修改 Stroke Modeler 参数、prediction、pressure、smoothing、L0/L1/L2、dirty rect、Present 或渲染路径。
- 调用或等待 `GetFrameLatencyWaitableObject()`、混合 pacing 策略、增加 DComp/DWM 同步等待，或把 `Present1` 返回解释为 photon latency。
- 根据当前源码或一次日志直接宣布根因或实施修复。
- 启动 GUI、模拟输入或代替用户执行真机绘制。

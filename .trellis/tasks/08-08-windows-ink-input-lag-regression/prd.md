# Windows Ink 输入回归诊断调查

## Goal

本任务不修复任何生产行为，只建立低扰动、可关联的输入诊断 session。第一阶段已确认真实 Pen 输入能够稳定到达 DrawingController / Stroke Modeler；第二阶段继续回答一个成功进入 Modeler 的 point 何时形成 geometry、触发 render，并提交给 DXGI `Present1`。

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

## Out Of Scope

- 修改 state gate、decoder lifecycle、cursor wake、frame deadline、coordinator、contact queue/seqlock 或线程优先级。
- 修改 Stroke Modeler 参数、prediction、pressure、smoothing、L0/L1/L2、dirty rect、Present 或渲染路径。
- 调用或等待 `GetFrameLatencyWaitableObject()`、混合 pacing 策略、增加 DComp/DWM 同步等待，或把 `Present1` 返回解释为 photon latency。
- 根据当前源码或一次日志直接宣布根因或实施修复。
- 启动 GUI、模拟输入或代替用户执行真机绘制。

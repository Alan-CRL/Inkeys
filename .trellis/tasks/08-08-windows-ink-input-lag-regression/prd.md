# Windows Ink 输入回归诊断调查

## Goal

本阶段不修复任何生产行为，只建立一次低扰动、可关联的输入诊断 session。第一次真实 Surface Pen / Mouse A/B 后，日志必须能回答失败落笔停在 RTS Down、decoder/binding/state gate、coordinator publication，还是 DrawingController / Stroke Modeler。

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

## Acceptance Criteria

- [x] Down 和 Packets 所有目标早退路径具有独立 `RtsPacketResult`。
- [x] 正常 Packets 采样，失败事件尽量进入固定 timeline，全部 reason 始终进入 aggregate。
- [x] Pen/Mouse/Touch/Unknown 与 DrawingController correlation 字段写入同一时间线。
- [x] session 文件具有唯一默认路径和完整 BEGIN/END/build 标记。
- [x] 非 GUI 测试覆盖 session 边界、reason 文本、成功采样、Unknown 标签、timeline overwrite 以及 aggregate 保留。
- [x] reviewer auxiliary 修复后的完整解决方案 `Debug|ARM64` 构建和 Debug ARM64 测试通过。
- [x] reviewer auxiliary 修复后的完整解决方案 `Release|ARM64` 构建和 Release ARM64 测试通过。
- [x] 静态审计确认 callback 热路径没有新增 IO、分配、阻塞、COM、wake、wait 或生产行为变化。
- [ ] 用户完成第一轮真实 Pen 后，能够按时间重建 Down -> Packets -> Drawing -> Modeler 并与随后 Mouse 对照。

## Out Of Scope

- 修改 state gate、decoder lifecycle、cursor wake、frame deadline、coordinator、contact queue/seqlock 或线程优先级。
- 修改 Stroke Modeler 参数、prediction、pressure、smoothing、L0/L1/L2、dirty rect、Present 或渲染路径。
- 根据当前源码或一次日志直接宣布根因或实施修复。
- 启动 GUI、模拟输入或代替用户执行真机绘制。

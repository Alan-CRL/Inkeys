# RTS 笔尾橡皮支持

## Goal

通过 RTS 的倒转笔标志识别 MPP2.0 笔尾，使用户在当前选择画笔或荧光笔时可直接翻转笔杆擦除，同时提供运行时预留开关；关闭开关后笔尾继续按当前工具书写。

## Background

- 历史实现 `D:/Project/Inkeys/Repo/Inkeys/Inkeys/IdtRts.cpp:214` 使用 `StylusInfo::bIsInvertedCursor` 识别笔尾。
- 当前 RTS 数据链尚未把该标志发布到 `ContactSnapshot`，绘制线程只能按窗口选择锁定工具。
- MPP2.0 笔尾报告的压力可能只反映按下瞬间，不能作为稳定的橡皮压力或模型状态。

## Requirements

- `ContactSnapshot` 必须携带 `isInvertedCursor`，并通过现有 seqlock 一致快照协议发布 Down、Move、Up 和 Cancelled。
- RTS 的 Down、Move、Up 必须从当前回调的 `StylusInfo::bIsInvertedCursor` 写入快照；Mouse、Touch 和缺失状态保持 `false`。
- 新增默认开启的运行时笔尾橡皮开关；`StrokeModelConfiguration` 保存初始值，`DrawingController` 提供线程安全 Set/Get，设置更新只影响之后 Down 的笔画。
- 开关开启时，只有 `InputDeviceType::Pen`、倒转标志为真且当前选择为画笔或荧光笔的 Down 才把该 contact 锁定为橡皮；当前已经选择橡皮、Mouse 和 Touch 行为不变。
- 开关关闭时，倒转笔尾按当前画笔或荧光笔书写，不切换为橡皮。
- 所有倒转 Pen 无论开关状态都向 Stroke Modeler 传入未知压力 `-1`；普通笔硬件压力模式因此在 Down 回退模拟压感，荧光笔继续固定宽度。
- 倒转产生的橡皮必须复用现有 50px 固定宽度、无 prediction、直接提交真实点的橡皮路径，不实现压感橡皮。
- 有效工具、倒转状态、压力屏蔽和开关值均在 Down 时锁定；Move/Up 不允许中途切换工具或恢复压力。
- 同批多 contact 的窗口选择工具必须独立于每个 contact 的有效覆盖工具；倒转 Pen 不能让随后正常 Pen/Touch 继承橡皮。
- RTS 同步回调仍只做固定成本字段复制、快照发布和唤醒，不增加查询、分配或应用级锁。

## Acceptance Criteria

- [x] `isInvertedCursor` 在 Down、Move、Up、并发读取和 slot 复用中保持同一 sequence 的一致性。
- [x] 倒转 Pen + 开关开启在画笔/荧光笔下解析为橡皮；开关关闭、非倒转、Mouse、Touch 不触发覆盖。
- [x] 倒转状态下模型压力始终为 `-1`，普通笔真实压感设置回退模拟模式；非倒转压力链无回归。
- [x] 倒转 contact 与正常 contact 同批输入时，后者仍使用窗口选择的原始工具。
- [x] ARM64 `Debug|ARM64` 完整构建和 `inkStrokeModelerTestTests` 通过。
- [x] 用户使用 MPP2.0 真机验证画笔/荧光笔翻转擦除、开关关闭后笔尾书写、抬笔和切回正常笔尖无异常。

## Out Of Scope

- 不实现压感橡皮、笔尾独立宽度设置、外部设置 UI 或持久化。
- 不修改第三方 Ink Stroke Modeler，也不新增 Windows Ink 输入后端。

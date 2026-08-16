# RTS 笔压与笔姿态支持

## Goal

打通 RTS packet 到 contact 快照、Google Ink Stroke Modeler 和普通笔几何的真实笔压与笔姿态数据链，使支持压感/倾斜的 Pen 使用硬件压力绘制，同时保留 Mouse、Touch 和无压感 Pen 的稳定回退行为。

## Background

- 当前 RTS 仅请求 X/Y，`ContactSnapshot::pressure` 始终为 `-1`，普通笔统一使用速度模拟压感。
- Google Ink Stroke Modeler 已原生接受并输出归一化 `pressure`、`tilt` 和 `orientation`，无需修改 `additional/` 第三方源码。
- MPP2.0 笔的 4096 级压力不是固定协议上限；必须按设备 `PROPERTY_METRICS` 归一化，以兼容 8192 级及更高逻辑范围。

## Requirements

- RTS 必须继续要求 X/Y，同时可选请求 NormalPressure、X/Y Tilt、Azimuth 和 Altitude；可选属性缺失或扩展请求失败不得使 Mouse、Touch 或无压感 Pen 失去输入。
- `ContactSnapshot` 必须携带模型契约中的三项状态：压力 `[0,1]`、倾角 `[0,π/2]`、方位角 `[0,2π)`；未知值统一为 `-1`，并通过现有一致快照协议跨线程发布。
- 压力必须按 tablet 返回的逻辑最小/最大值归一化，不写死 4096、8192 或其他级数。
- 角度优先由 Azimuth/Altitude 转换；缺失时由 X/Y Tilt 推导。单位、分辨率或输入无效时对应字段保持未知。
- Pen 的压力、倾角和方位角必须在固定、模拟和真实宽度模式下都传入 Stroke Modeler；Touch/Mouse 不使用硬件笔状态。
- Mouse 与 Touch 分别支持固定宽度和模拟压感；Pen 支持固定宽度、模拟压感和真实压感。默认 Mouse/Touch 为模拟压感、Pen 为真实压感。
- 必须提供线程安全的成组运行时宽度模式 Set/Get 接口；更新只影响之后 Down 的普通笔笔画，活动笔画保持起笔时模式。
- 荧光笔和橡皮继续固定 50px，不受设备宽度模式影响。
- Pen 真实模式在 Down 无有效压力时整笔回退模拟压感；进入真实模式后偶发压力缺失沿用上一有效值，不在笔画中途切换模式。
- 5px 普通笔的真实压力直径必须按 `5 × (0.2 + 1.2 × pressure)` 线性映射为 1–7px，并保留现有相邻半径几何约束。
- 真实建模点使用模型插值后的压力；预测点冻结最后真实半径后继续使用现有 L0 笔锋 taper。Down 后立即 Up 的点必须使用 Down 压力。
- RTS 同步回调仍只能执行固定成本 packet 解码、快照发布和唤醒，不增加 COM 查询、动态分配或应用级锁。

## Acceptance Criteria

- [x] 4095、8191 等不同逻辑上限能归一化为相同 `[0,1]` 压力，越界值被夹取，无效指标得到 `-1`。
- [x] 度/弧度单位、Azimuth/Altitude、X/Y Tilt 推导和方位角环绕均有确定性测试。
- [x] pressure/tilt/orientation 在 Down、Move、Up、并发竞争和 slot 复用中保持同一 sequence 的一致快照。
- [x] 设备/工具宽度模式矩阵、默认值、运行时更新只影响新笔画和真实压力缺失回退均有自动化测试。
- [x] 压力 0/0.5/1 分别生成 1/4/7px 直径；短点击、预测尾宽冻结、固定宽度和模拟压感无回归。
- [x] ARM64 `Debug|ARM64` 完整构建 `inkStrokeModelerTest.sln` 成功，`inkStrokeModelerTestTests` 全部通过。
- [x] MPP2.0 真机验证轻压/重压、倾斜/旋转和抬笔收尾无异常。

## Out Of Scope

- 不新增 `Windows.UI.Input.Inking` 输入后端。
- 不实现外部设置 UI、配置持久化或活动笔画即时切换。
- 不传输 Twist、切向压力、桶按钮或接触面积，也不修改第三方 Ink Stroke Modeler。

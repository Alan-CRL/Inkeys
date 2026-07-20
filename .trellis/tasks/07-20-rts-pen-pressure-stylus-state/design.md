# RTS 笔压与笔姿态支持设计

## Boundaries

改动限定在 `draw3` 的 RTS packet 元数据/解码、contact 一致快照、Stroke Modeler 输入和普通笔宽度策略。`additional/` 第三方模型、渲染器 GPU 契约、L0/L1/L2 提交语义、荧光笔和橡皮几何保持不变。

## Public configuration

`draw3.ink_prediction` 新增：

- `InputWidthMode { Fixed, SimulatedPressure }`：供 Mouse、Touch 独立选择。
- `PenInputWidthMode { Fixed, SimulatedPressure, HardwarePressure }`：供 Pen 选择。
- `InputWidthModeSettings`：默认 Mouse/Touch=`SimulatedPressure`，Pen=`HardwarePressure`。

`StrokeModelConfiguration` 携带初始设置。`DrawingController` 暴露成组 `SetInputWidthModeSettings`/`GetInputWidthModeSettings`；内部把三个枚举编码进单个 32 位原子快照，setter 验证全部枚举后一次发布，getter 始终返回同一版本的完整配置。每个普通笔 contact 在 Down 时读取并解析一次，活动笔画不受后续设置变化影响。

## RTS metadata and decoding

`SetDesiredPacketDescription` 首先请求 X、Y、NormalPressure、X/Y Tilt、Azimuth、Altitude；失败时记录一次诊断并重试仅 X/Y。X/Y 仍是初始化成功的最低要求。

每个 `TabletMetadata` 在慢路径缓存可选属性的 index 和 `PROPERTY_METRICS`，填充完毕后再 release 发布。Move 热路径只读取已发布缓存：

- Pressure：`clamp((raw-min)/(max-min), 0, 1)`；范围无效时为 `-1`。
- Angle：按 `fResolution` 将 `PROPERTY_UNITS_DEGREES` 或 `PROPERTY_UNITS_RADIANS` 转为弧度；其他单位或无效分辨率为未知。
- Azimuth/Altitude 同时有效时，`tilt=clamp(π/2-altitude,0,π/2)`，`orientation=wrap(2π-azimuth)`，完成 RTS 顺时针到模型逆时针转换。
- 否则在 X/Y Tilt 同时有效时，以 `tan(xTilt)`、`tan(yTilt)` 得到投影，`tilt=atan(hypot(...))`，`orientation=wrap(atan2(-tan(yTilt),tan(xTilt)))`。

只有 `InputDeviceType::Pen` 发布三项状态；Touch/Mouse 保持 `-1`。`ContactSnapshot` 和 `ContactRecord` 增加 tilt/orientation 的无锁浮点标量，继续受现有奇偶 sequence 和 writer latch 保护。

## Model and width data flow

Runtime stroke 保存最后有效 pressure/tilt/orientation。Down 之后偶发缺值沿用上一有效值，避免第三方模型把某字段永久标记为 unknown。三项状态写入每个 `ink::stroke_model::Input`，并启用 `stylus_state_modeler_params.use_stroke_normal_projection`。

普通笔 Down 时解析内部 `StrokeWidthMode`：

- MouseLeft/MouseRight：读取 mouse 设置。
- Touch：读取 touch 设置。
- Pen：读取 pen 设置；`HardwarePressure` 仅在 Down 压力有效时成立，否则锁定为 `SimulatedPressure`。
- 非普通笔工具始终为 `Fixed`。

硬件压力结果按 `baseDiameter × (0.2 + 1.2 × clamp(pressure,0,1))` 生成目标直径，并复用现有相邻半径约束。Down 起点半径使用同一规则，保证点击点正确。预测几何始终复制最后真实半径，再由现有 L0 taper 处理；Up 的真实模型结果使用模型压力，不叠加新的完成态 taper。

## Compatibility and rollback

- 无压感 Pen、扩展 packet 请求失败或无效 PROPERTY_METRICS 均回退现有模拟压感。
- Mouse/Touch 默认仍为模拟压感，荧光笔/橡皮保持固定宽度。
- 若 RTS 扩展属性造成平台问题，可局部回滚为 X/Y 请求；snapshot 新字段和模式枚举仍可保留为 unknown/回退状态。
- 不改变持久化格式或 GPU 结构，无数据迁移。

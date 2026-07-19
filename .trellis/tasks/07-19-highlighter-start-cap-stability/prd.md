# 荧光笔起笔端帽稳定性

## Goal

消除荧光笔起笔初段方向未锁定时的错误可见几何，保证 start cap 一旦首次可见，其锚点和方向在后续 Move、L1 分层提交与 Up/L2 完成态中保持不变。

## Requirements

### R1. 首次可见条件

- 几何边界包含全局 Start 且 `startDirectionState` 尚未锁定时，不生成活动荧光笔几何。
- 真实路径累计达到 12px 后，以锁定弦方向首次显示。
- 预测点不参与 12px 资格判定，也不能提前决定 start cap。

### R2. 稳定方向

- 方向锁定后不可被后续 Move、曲线方向、L1 切片或 Up 改写。
- start cap 的锚点保持为真实按下点；内部切片不应被误认为全局 Start。
- 活动态、稳定前缀和完成态统一调用同一几何规则。

### R3. 短划

- 最终真实路径不足 12px 时，活动态始终不可见。
- 只在 Up 时生成从按下点出发、使用确定方向的 `12×50px` short mark。
- Cancelled 不应凭空生成 short mark。

### R4. 自动化回归

- 覆盖不足 12px、恰好达到/刚超过 12px、长直线、长曲线和重复点。
- 比较首次可见 start cap 的锚点/方向与追加点后、L1 切片后和完成态结果。
- 几何测试直接调用生产实现，不复制判定算法。

## Acceptance Criteria

- [x] 不足 12px 的活动荧光笔不产生 primitives/bounds，Up 生成确定 short mark。
- [x] 达到 12px 的第一帧使用锁定方向，后续追加点不改变全局 start cap。
- [x] L1 切片和 Up/L2 完成态保留相同 start anchor/direction。
- [x] 直线、曲线、重复点及阈值边界自动化测试通过。
- [x] 普通笔、橡皮和荧光笔非起端几何不发生无关变化；人工视觉确认旧“缺角”来自橡皮擦除。

## Out of Scope

- 不改变 12px、50px、2px overlap、0.5°/177° 等现有实验参数。
- 不重写高亮 GPU primitive 或 HLSL。
- 不修改 prediction、宽度估算和透明呈现。

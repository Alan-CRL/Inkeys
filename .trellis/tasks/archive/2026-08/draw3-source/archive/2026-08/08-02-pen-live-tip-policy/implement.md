# Implement: 普通笔实时笔锋恢复与设备策略

## Checklist

1. [x] `ink_prediction`：新增仅空间公切线安全 `EnforceCapsuleTangency`（双向投影，slope 0.95）；`RebuildL0DrawPoints` 在 taper 后改用它，不再调用含时间项的 `LimitRadiusTransitions`。
2. [x] `drawing_controller` 主循环与 `DrawMouseStroke`：按 `widthMode == HardwarePressure` 将 taper 时长置 0；commit/idle 保护时长仍用配置 liveTip（工具层除外）。
3. [x] 默认抬笔定住真实尾 + tip（去 prediction）；`retainPredictionOnUp=true` 仍可保留完整 L0。
4. [x] 测试：HardwarePressure 不 taper；SimulatedPressure/Fixed 会 taper；taper 后相邻点 `|Δr| < length`；抬笔尾含 tip。
5. [x] `Debug|ARM64` 构建通过；相关自动化测试通过。
6. [x] 更新 `.trellis/spec/native/runtime-and-rendering.md` 中 tip 策略与 L0 约束描述。

## Validation

- 单元：`contact_input_tests` / `highlighter_geometry_tests` 覆盖 tip 策略、公切线与抬笔定住。
- 构建：`MSBuild ... Debug|ARM64` 成功。
- 人工：Mouse/Touch 绘制中可见 tip，抬笔后 tip 定住；HardwarePressure 无 tip。

## Work commit

- `b7af67f` fix: restore pen live tip and freeze tip on up

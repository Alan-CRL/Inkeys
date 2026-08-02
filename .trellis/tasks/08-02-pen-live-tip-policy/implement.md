# Implement: 普通笔实时笔锋恢复与设备策略

## Checklist

1. [x] `ink_prediction`：新增仅空间公切线安全 `EnforceCapsuleTangency`（双向投影，slope 0.95）；`RebuildL0DrawPoints` 在 taper 后改用它，不再调用含时间项的 `LimitRadiusTransitions`。
2. [x] `drawing_controller` 主循环与 `DrawMouseStroke`：按 `widthMode == HardwarePressure` 将 taper 时长置 0；commit/idle 保护时长仍用配置 liveTip（工具层除外）。
3. [x] 测试：HardwarePressure 不 taper；SimulatedPressure/Fixed 会 taper；taper 后相邻点 `|Δr| < length`。
4. [x] `Debug|ARM64` 构建通过；`inkStrokeModelerTestTests.exe` 待本轮运行记录。
5. [x] 更新 `.trellis/spec/native/runtime-and-rendering.md` 中 tip 策略与 L0 约束描述。

## Validation

- 单元：`contact_input_tests` 增加 tip 策略与公切线断言。
- 静态：L0 路径使用 `EnforceCapsuleTangency`；`ResolveLiveTipTaperDurationSeconds(HardwarePressure)=0`。
- 构建：`MSBuild ... Debug|ARM64` 成功，VS/PS/UpdateCS/EmitCS 均编译。
- 人工：Mouse/Touch 笔锋、有压感 Pen 无 tip — 待用户验证。

## Risky files

- `inkStrokeModelerTest/draw3/ink_prediction.cpp`
- `inkStrokeModelerTest/draw3/ink_prediction.cppm`
- `inkStrokeModelerTest/draw3/drawing_controller.cpp`
- `inkStrokeModelerTestTests/contact_input_tests.cpp`

## Rollback

单 commit 回滚上述文件 + spec 即可。

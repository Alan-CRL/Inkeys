# 普通笔实时笔锋恢复与设备策略

## Goal

恢复鼠标与手指（及 ink 笔回退模拟压感）在普通笔下的可见实时笔锋；仅 `HardwarePressure` 普通笔禁用 L0 实时笔锋。笔锋作为 L0 叠加效果，不再被稳定笔宽时间限速抹平，同时用公切线安全约束防止胶囊露端。

## Background / Confirmed Facts

- 当前普通笔 L0 路径：`real/predicted points → ApplyLiveTipTaper → LimitRadiusTransitions`（`ink_prediction.cpp:1407-1429`）。
- 实时笔锋只按工具关闭：橡皮/荧光笔 `liveTipDurationSeconds = 0`；激光笔显式传 `0.0`。没有“仅 HardwarePressure 关 tip”的分支。
- Mouse/Touch 默认 `SimulatedPressure`，Pen 默认 `HardwarePressure`（Down 压力有效时）；压力无效时整笔回退 `SimulatedPressure`。
- 稳定笔宽限速当前为：时间 `0.5 * baseDiameter * 3.0 * dt`、空间 `0.35 * distance`。该限速同时作用于 width estimator 与 L0 taper 之后，导致 5px 下 tip 几乎不可见。
- L1 稳定前缀来自未 taper 的 `realPoints`；taper 只存在于 L0。
- 胶囊几何硬约束：`|r1-r2| < segmentLength`；shader 在 `|r1-r2| >= h` 时回退较大端点圆。

## Requirements

### R1. Tip 策略（按宽度模式，非按设备）
- `StrokeWidthMode::HardwarePressure` + 普通笔：禁用 `ApplyLiveTipTaper`。
- `SimulatedPressure` / `Fixed` + 普通笔（含 Pen 回退模拟压感、Mouse、Touch）：启用实时笔锋，手感应尽量接近最初实现。
- 荧光笔 / 橡皮 / 激光笔：保持现状。

### R2. 约束分层
- 稳定笔宽限速（时间 + 空间）只约束 `realPoints` 生成（L0→L1 稳定宽度）。
- 实时笔锋在当前 L0 几何上叠加；taper 后**不再**套用稳定笔宽时间限速。
- taper 后仅做胶囊公切线安全约束，防止露端帽，并尽量保留原始 tip 目标形状。

### R3. 保护窗口
- `CommitStablePrefixToL1` 的 live 保护时长仍使用配置的 `liveTipDurationSeconds`（含 HardwarePressure），避免尾部压感变化过早烘干；与是否绘制 tip 解耦。

### R4. 抬笔定住
- 默认抬笔：最终真实尾段 + 与绘制中相同的 tip taper 合入 L1，去掉 prediction，避免抬笔瞬间 tip 消失/回缩。
- `retainPredictionOnUp=true`：保留最后可见 L0（可含 prediction）。
- 完成态 tip 不走稳定笔宽时间限速；仅做与 L0 相同的公切线安全投影。

## Acceptance Criteria

- [x] Mouse / Touch + 普通笔：实时笔锋明显可见。
- [x] Pen + HardwarePressure：不叠加 L0 tip taper。
- [x] Pen 回退 SimulatedPressure：与鼠标一样启用 tip。
- [x] 稳定 `realPoints` 仍受既有笔宽限速。
- [x] L0 tip 段满足公切线安全，不露端帽。
- [x] 默认抬笔后 tip 仍保留，不因回退未 taper 的 realPoints 而消失。
- [x] 荧光笔 / 橡皮 / 激光不回归。
- [x] 测试覆盖 tip 开关策略、抬笔定住与 taper 后几何约束。

## Out of Scope

- 改变硬件压感 1–7px 映射。
- 荧光笔/橡皮引入 tip。
- UI 运行时 tip 开关。

## Key Decisions

- Tip 开关：仅 `HardwarePressure` 关闭；`SimulatedPressure`/`Fixed` 开启。
- 稳定限速与 tip 安全约束分离；tip 后不做时间限速。
- 笔锋目标曲线尽量保持原始 `ApplyLiveTipTaper`（末端约 0.28×）。

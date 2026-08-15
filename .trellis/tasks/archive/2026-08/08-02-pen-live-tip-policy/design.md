# Design: 普通笔实时笔锋恢复与设备策略

## Boundaries

- 改动集中在 `draw3.ink_prediction` 与 `draw3.drawing_controller`。
- 不改 shader、不改硬件压感映射、不改 Laser/Highlighter/Eraser 语义。

## Contracts

### Tip 开关

| `StrokeWidthMode` | 普通笔 L0 `ApplyLiveTipTaper` |
|---|---|
| `HardwarePressure` | 关（`liveTipTaperSeconds = 0`） |
| `SimulatedPressure` | 开 |
| `Fixed` | 开 |
| `LaserPressure` | 不适用（Laser 已强制 0） |

开关依据是 **widthMode**，不是 deviceType。Pen Down 无压力回退 `SimulatedPressure` 时 tip 开启。

### 保护窗口与 tip 解耦

- `CommitStablePrefixToL1` / idle freeze 继续使用配置 `liveTipDurationSeconds`（工具层：橡皮/荧光笔为 0）。
- 仅 `RebuildL0DrawPoints` 的 taper 时长在 `HardwarePressure` 时置 0。
- 这样 HardwarePressure 尾部压感变化仍有时间保护，但不叠加 tip 收细。

### 半径处理两阶段

```
realPoints  <- StrokeWidthEstimator: 时间+空间稳定限速（不变）
l0DrawPoints <- real_tail + prediction
            <- ApplyLiveTipTaper(liveTipTaperSeconds)   // 仅 L0 叠加
            <- EnforceCapsuleTangency(spatial-only)     // 替代 taper 后的 LimitRadiusTransitions
```

`LimitRadiusTransitions`（含时间项）**不再**在 L0 taper 后调用。

### EnforceCapsuleTangency

- 仅空间约束：`|Δr| <= kCapsuleRadiusSlope * distance`，`kCapsuleRadiusSlope = 0.95`（`< 1` 保证公切线）。
- 双向投影（先向前再向后），优先在安全包络内逼近 taper 目标，避免单向前向把末端夹回粗。
- 零长度段：半径对齐前一点（或取较大端点），避免 NaN/露端。
- tip 目标曲线保持现有 `ApplyLiveTipTaper`（末端约 0.28×、spanRatio 逻辑不变）。

## Data flow

1. Down：`ResolveStrokeWidthMode` 锁定 `widthMode`（已有）。
2. 帧循环：
   - `liveTipProtection = eraser|highlighter ? 0 : config.liveTip`
   - `liveTipTaper = (eraser|highlighter || widthMode==HardwarePressure) ? 0 : config.liveTip`
   - Commit 用 protection；RebuildL0 用 taper。
3. `DrawMouseStroke` 同步同一策略。

## Compatibility

- 默认 Mouse/Touch 行为：tip 恢复可见。
- 默认 Pen 有压力：无 tip，仅硬件压感。
- 默认 Pen 无压力回退：与 Mouse 同 tip。
- `retainPredictionOnUp` 语义不变。

## Trade-offs

- 空间斜率 0.95 比旧 0.35 更允许 tip 收细；仍严格 `< 1` 防露端。
- 双向投影略增 L0 点列 O(n)，n 仅为 tail+prediction，可接受。
- HardwarePressure 保护窗仍含 liveTip 时长，尾部稍晚进 L1；与现 commit 语义一致，避免压感尾被过早烘干。

## Rollback

- 恢复 `RebuildL0DrawPoints` 在 taper 后调用 `LimitRadiusTransitions`。
- 去掉 widthMode tip 分支。

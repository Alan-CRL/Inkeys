# Surface 面积参考失配证据（2026-09-27）

## 文件与可见范围

三份用户日志分别为 `Pasted text(10).txt`（Mouse）、`Pasted text (2).txt`（Touch 面积关）、`Pasted text (3).txt`（Touch 面积开）。可见、正在擦除的 `[EraserInput]` 快照分别为 84/184/100 条；Surface 显示快照为 2880×1920、192 DPI、EDID 28×19cm，B=32。所有这些快照的 `cursorDiameterPx` 与 `geometryDiameterPx` 在记录精度内相等，`|2×actualDip−geometryDiameterPx|` 最大约 0.001px。它们是约 250ms 的诊断输出，不能视作原始采样包或完整中间帧。

面积开启时，`contactId=54/gen=1` 在 seq34 已 `ready`，参考下限 60.413 DIP；后续普通拖动的实际直径约 30.670→55.587→59.855→60.413 DIP。它证明面积辅助已在清扫证据很低时独立起效，并不构成调大面积倍率的理由。

`contactId=55/gen=2` 的 Down（seq303）面积为 23.440×43.257 DIP；seq333 的可见样本 47.976×51.122 DIP，参考已建立、参考下限 55.768 DIP，实际 38.395 DIP。seq364 开始，53.647×52.558 DIP 被标为 `outlier`，参考仍 `ready`，实际 52.434 DIP、接受下限 45.160 DIP。seq394/424/484 的可见样本仍约 50–54×52–56 DIP 且继续 `outlier`；接受下限 23.263→17.808→16.111 DIP。seq515 之后下限约 16 DIP，速度目标仍可能让实际尺寸正常增大。日志里的每个样本都通过了可见的单位/绝对大小路径；无法仅凭这些稀疏样本判定真实指姿还是驱动暂态。

## 已定位状态机边界

`ObserveContactArea` 在已锁存参考后，对两轴分别执行 `max(样本,参考) <= min(样本,参考)×outlierRatio+1 DIP`。失败时保留旧 `referenceWidth/Height` 和 `referenceReady`，设置失效时间并返回；下方初始候选逻辑永远无法从连续失配样本重新建立新参考。`AreaExpirySeconds` 的无效宽限与 `AreaReferenceFloor` 的平滑释放属于原安全合同。缺少的是硬检查已通过、真实拖动且自身稳定的新样本的有限恢复途径。

参考下限 55.768 DIP 与既有倍率 1.10、padding 6 对应最初参考较大轴约 45.244 DIP（低于 64 DIP 上限，属于推算）。但现有日志没有锁存的 `referenceWidth/Height`，不能确证是宽轴还是高轴触发相对检查；后续诊断需直接输出两轴参考与失配轴/比例。`contactId=53/gen=1` 只有短暂离群可见，不据此声称存在同样长的用户可见失效。

# RTS 笔尾橡皮支持设计

## Data flow

```text
RTS StylusInfo::bIsInvertedCursor
  -> ContactSnapshot::isInvertedCursor
  -> ContactRecord seqlock snapshot
  -> DrawingController Down policy
  -> effective tool + pressure suppression locked per RuntimeStroke
  -> existing eraser or selected drawing path
```

RTS 回调只复制 `bIsInvertedCursor`。原始压力仍保留在快照中，绘制线程根据 Down 锁定的倒转状态决定是否把模型压力改为 `-1`，从而保留输入诊断能力并避免修改第三方模型。

## Public contracts

- `ContactSnapshot::isInvertedCursor`：默认 `false`，属于一致快照的一部分。
- `StrokeModelConfiguration::invertedPenEraserEnabled`：启动默认值，默认为 `true`。
- `DrawingController::SetInvertedPenEraserEnabled/GetInvertedPenEraserEnabled`：线程安全运行时开关，新值只在 Down 读取。
- 纯策略 helper 返回倒转 Pen 是否请求橡皮覆盖及模型压力是否应屏蔽，供自动化测试覆盖完整矩阵。

## Tool resolution

每个 `RuntimeStroke` 同时保存：

- `selectedTool`：当前输入批次锁定的窗口选择，用于随后 contact 继承。
- `tool`：本 contact 的最终工具；仅倒转 Pen 在开关开启且 `selectedTool` 为 Pen/Highlighter 时覆盖为 Eraser。
- `suppressPressure`：Down 时由 Pen + inverted 决定，整个 contact 保持不变。

分离两个工具字段可避免第一个倒转 Pen 把同批随后正常 contact 的批次工具错误改成 Eraser。

## Pressure behavior

倒转 Pen 的 Down 压力先转换为 `-1` 再解析宽度模式和构造模型输入；Move/Up 同样固定为 `-1`，且压力变化不作为独立模型更新原因。倾角和方位角继续传入模型。

- 开关开启：有效工具为 Eraser，复用现有固定 50px、禁用 prediction 路径。
- 开关关闭 + Pen：有效工具仍是 Pen，HardwarePressure 因 Down 压力未知而整笔回退 SimulatedPressure。
- 开关关闭 + Highlighter：保持固定 50px Highlighter。

## Compatibility and rollback

非倒转 Pen、Mouse、Touch 不改变。若设备驱动不提供倒转标志，字段默认 `false`。回滚时可移除策略覆盖而不改变现有压力、姿态、contact pool 或渲染器契约。

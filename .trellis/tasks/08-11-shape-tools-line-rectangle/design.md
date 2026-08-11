# Technical Design

## Architecture

Shape 沿用现有输入线程与绘制线程边界，不新增 D3D 资源：

```text
WM_KEYDOWN Q/W/E/R
  -> DrawingTool atomic selection
  -> contact Down locks tool and raw start
  -> Stroke Modeler Update/Predict produces only current endpoint
  -> ShapePrimitive batch -> L0 operator
  -> Up stores exactly two points
  -> DrawStoredStroke -> L2 / history tile / page restore
```

`DrawingTool` 和 `StoredInkType` 只在尾部追加新值，保留现有数值。GPU 新增 `ShapePrimitiveKind`：SolidLine=16、DashedLine=17、OutlineRectangle=18、FilledRectangle=19。

## Runtime State And Data Flow

- 每个 Shape runtime 保存固定起点、最后真实/建模终点、当前预测终点、前后 dirty bounds、primitive kind 和有效标志。
- 继续复用预热的 Stroke Modeler；`modeledResults` 与 `predictedResults` 作为 scratch。每次 Update 后仅提取最后位置并清空，不生成 `realPoints/predictedPoints/l0DrawPoints`。
- Down 起点直接使用原始输入。活动帧调用 Predict，最后 prediction 覆盖当前 L0 终点；失败时回退建模/原始终点。Up 仍送入模型完成生命周期，但最终几何强制使用原始 Up。
- Shape 分支跳过 `CommitStablePrefixToL1`。任一 Shape 几何变化时清空 L0 并把全部活动 Shape 按同一 kind 批量重画；没有变化时保留 L0。完成、Cancel、resize 和 page switch 仍通过现有全量活动层重建入口恢复一致性。
- Shape 使用 Pen cursor/color/haptic；选择 Shape 时倒转 Pen 可进入既有 Eraser 路径。Shape 不参加断触 reconnect，物理 Up 直接完成该几何。

## Renderer And Shader Contract

- `ShapePrimitive` 为 32 字节，内存布局等价于两个连续 `InkPoint`；起点槽保存起点和半线宽，终点槽保存终点。renderer 复用 `inkDataBuffer`，每个 primitive 对应 6 个顶点。
- `DrawShapePrimitives` 以 `kMaxBufferCapacity / 2` 分批，Map 后一次上传连续 primitive，并设置追加的 shape type；不创建新 buffer、纹理或 SRV。
- VS 对直线生成扩张 OBB，对矩形用 `min/max` 生成扩张 AABB。PS 使用 analytic SDF：实线胶囊、周期虚线胶囊、rounded-box fill、`abs(rounded-box distance) - halfWidth` 居中边框。
- 矩形圆角像素值由 renderer 配置的 `8 * dpiScale` 写入既有 `globalPadding.x`；小矩形在 shader 内钳制。CPU/HLSL 常量缓冲区大小不变。
- 新路径使用现有 stroke operator blend state。启动时在零像素 viewport 依次预热四个 Shape 分支，避免首次使用的驱动 JIT 卡顿。

## Persistence And History

- `StoredInkType` 追加四类 Shape。完成函数拒绝退化矩形；其他 Shape 构造两个 `StoredInkPoint`，两点宽度相同。
- `DrawStoredStroke` 根据类型构造单个 `ShapePrimitive`，平移到 full-canvas 或 tile-local origin 后调用同一个 renderer API。
- `BuildStrokeTileFootprint` 按类型分派：Line 使用 segment traversal；OutlineRectangle 对四边分别 traversal 后去重；FilledRectangle 添加规范化矩形覆盖的全部可见 tile。
- pixel bounds 使用与 shader 相同的半线宽和 AA padding；圆角不会超出矩形/边框的保守包络。

## Failure And Compatibility

- Predict 失败只清空 prediction 并回退真实终点；模型 Update 失败保留最近原始坐标，Up 仍可完成。
- Cancel 不追加文档，旧 L0 bounds 必须进入 frame dirty。
- renderer Map/Draw 失败沿用 Stored raster failure 日志与 history 回退，不提交不完整的可见缓存。
- 不改变 shape 0..15、buffer stride、寄存器、blend state、L2 预乘格式或现有 Stored 类型语义。

# 直线与圆角矩形绘制工具

## Goal

为 Draw3 增加低延迟的实线、虚线、边框圆角矩形和填充圆角矩形工具。活动手势只在 L0 显示预测终点，抬笔后进入现有文档、撤回、页面恢复和 GPU history 链路。

## Background

- 当前 `DrawingTool` 只有 Pen、Highlighter、Eraser 和 Laser，数字键选择工具。
- 普通墨迹活动期使用 L1 稳定前缀与 L0 实时尾部；Shape 必须保持完整几何只在 L0，不能提交稳定前缀。
- 完成对象必须先追加到 `InkCanvas`，再从同一个 Stored 对象重放到 L2；撤回和翻页依赖 `StrokeTileFootprint` 与 `DrawStoredStroke`。
- 现有 renderer 使用单个动态 `InkPoint` structured buffer 和 `globalShapeType` CPU/HLSL 协议。

## Requirements

- `Q` 选择实线、`W` 选择虚线、`E` 选择边框矩形、`R` 选择填充矩形；活动批次继续锁定首个 Down 时的工具。
- 四种 Shape 使用主画笔基础直径、颜色、光标和 Pen 触觉，不使用压力、倾角、笔锋或 taper；倒转 Pen 仍可临时覆盖为橡皮。
- 落笔原始坐标固定为起点；活动时终点优先取 Stroke Modeler 最后一个预测结果，其次取最后建模结果或原始输入；完成时必须用原始 Up 坐标作为最终终点。
- 活动 Shape 只绘制到 L0，不向 L1 提交；同类多 contact 必须批量上传和绘制，稳定帧不得重复重建相同 L0 几何。
- 实线为圆头胶囊；虚线为圆头、中心线实线段 `4 × 线宽`、中心线空隙 `6 × 线宽`，使圆头侵占后的可见线段与可见空隙接近 `1:1`，不得在 CPU 展开短划数组。
- 矩形支持从任意角向任意方向拖动；边框沿对角点定义的边界居中；填充矩形没有额外边框。
- 两种矩形使用 `4 DIP` 圆角，按当前 DPI 转换并钳制到短边一半。
- 完成 Shape 只保存两个端点、固定宽度、颜色和透明度，并通过统一 Stored 重放入口适配首次 L2 提交、撤回、翻页、resize 和冷重建。
- history footprint 必须保守覆盖 shader 实际像素：直线覆盖扩宽线段，边框矩形覆盖四边，填充矩形覆盖完整面积。
- 保持现有 shader shape 编号、预乘 Alpha 和仿射 operator 数学不变，仅在尾部追加 Shape 协议。
- 保持原文件 UTF-8 BOM/CRLF，并为关键跨层逻辑添加简短中文注释。

## Acceptance Criteria

- [ ] `Q/W/E/R` 可分别绘制实线、虚线、边框圆角矩形和填充圆角矩形，选择在活动批次中保持锁定。
- [ ] 起点不随模型漂移；活动终点使用 prediction；Up 后最终对象终点等于真实抬笔坐标。
- [ ] 活动 Shape 不污染 L1，L0 清理、预测回缩、Cancel、resize 和多 contact 后无残影。
- [ ] 虚线可见线段/空隙接近 `1:1`、圆头、4 DIP 圆角、任意方向拖动、居中边框与填充效果符合要求。
- [ ] 一条完成 Shape 在文档中恰好保存两个点，并能通过 `5` 撤回、`0/8` 翻页恢复。
- [ ] 填充矩形的内部 tile 被 history 覆盖；边框矩形不会无条件占用全部内部 tile。
- [ ] 零长度直线显示圆点；宽或高为零的矩形不写入文档或 history。
- [ ] 活动 Shape 状态不随路径长度增长；预热后热路径无逐帧堆分配，同类 Shape 一批一次 draw。
- [ ] ARM64 `Debug` 和 `Release` 完整解决方案构建成功，自动测试及 `--drawing-perf` 通过，四个 shader 资源编译/嵌入成功。

## Out of Scope

- Redo、UInk 编解码、外部 Shape 文件兼容。
- 颜色、线宽、圆角或虚线比例设置 UI。
- 修改现有 Pen、Highlighter、Eraser、Laser 的视觉参数或文档语义。

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.

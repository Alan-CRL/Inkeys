# barShape*.svg 图标审计（2026-11-05）

- 范围：`Inkeys/src/UI/barShape*.svg`，共 **54** 个。
- 基准：P2 的 `barShapeRectangle.svg`。
  - 24×24；主描边 2.6，圆端点、圆连接。
  - 封闭图形单独加一层 `rgba(9,0,2,0)` 填充，透明度 0.24。
  - 描边中线范围约 4–20 / 5–19，外沿范围约 2.7–21.3。
  - 颜色只用 `rgba(10,0,7,0)` / `rgba(9,0,2,0)`。运行时 color2 跟随 color1，见 `RetargetBarButtonInteractionVisual`。
- 草稿位置：`Build/shape-ux-icons/draft/`（隐藏棱间隙 1.5）和 `Build/shape-ux-icons/alt-gap08/`（只有立体图标不同，间隙 0.8）。
- 预览：`Build/shape-ux-icons/preview.html`，截图为 `preview.png` 和 `preview-solids.png`。
- 生成器：`Build/shape-ux-icons/tools/gen_icons.py <out> [gap]`；预览页：`build_preview.py`。

## 汇总

| 分组 | 数量 | 文件 |
|---|---|---|
| 合规（保持不变） | 25 | Angle, Arrow, Cartesian, CoordinatePoint, Cosine, DashDot, Dashed, Dotted, Exponential, Fill, Grid, Hyperbola, Logarithmic, NumberLine, Outline, Parabola, Parallel, Perpendicular, Polyline, Reciprocal, Rectangle, Rotate, Sine, Solid, Tangent |
| 需重绘：基准族只改填充透明度 | 7 | Ellipse, OutlineFill, Parallelogram, Rhombus, RoundedRectangle, Trapezoid, Triangle |
| 偏离基准但保留（用户决定） | 1 | StraightLine（描边 3.2） |
| 需重绘：封闭图形缺填充或尺寸不对 | 15 | Polygon, Polygon3–12（10 个）, Sector, CircleCenter, CircleBounds, Arc |
| 需重绘：立体 | 6 | Cube, Cuboid, Cylinder, Cone, Prism, Pyramid |
| **合计已替换** | **28** | 审计时判定需重绘 29 个；StraightLine 由用户决定保留 |

PRD 写的是"8 个图标带 24% 填充"，但实际只有 Rectangle 是 0.24。OutlineFill 是 0.28，其余 6 个都是 **0.16**。

## 明细

| 文件 | 结论 | 原因 / 草稿处理 |
|---|---|---|
| Rectangle | 合规 | 基准 |
| Ellipse | 需重绘 | 填充 0.16，改为 0.24 的独立填充层。几何不变 |
| RoundedRectangle | 需重绘 | 同上 |
| Triangle | 需重绘 | 同上 |
| Parallelogram | 需重绘 | 同上 |
| Rhombus | 需重绘 | 同上 |
| Trapezoid | 需重绘 | 同上 |
| OutlineFill | 需重绘 | 填充 0.28，改为 0.24。属于样式按钮，rx=2 与 Outline/Fill 一致，保留 |
| StraightLine | 保留（用户决定） | 描边 3.2 偏离 2.6 基准，但用户决定保留，用来平衡单线的视觉重量；不替换 |
| Polygon | 需重绘 | 封闭图形没有填充；补 0.24 填充，整体移动 0.5 让包围盒居中 |
| Polygon3–12 | 需重绘 | 都没有填充，补 0.24。奇数边原来按外接圆心放置，包围盒偏上（三角形偏 2.1），改为按包围盒居中，中心点跟着移动。三角形 R 从 8.5 放大到 9.5，弥补视觉面积偏小 |
| Sector | 需重绘 | 封闭图形没有填充；补填充，几何不变 |
| CircleCenter | 需重绘 | 封闭圆没有填充；补填充。r=7.5 和中心点不变 |
| CircleBounds | 需重绘 | 没有填充；外接框是 0.85 宽的整圈虚线，和圆外沿只隔约 0.3，24px 下贴在一起。改为 4 个 1.4 宽的角标，与圆至少隔 1.5，并补填充。r=7.5 不变，同一按钮切换时不跳 |
| Arc | 需重绘 | 开放曲线，不加填充是对的；但 r=9 时外沿到 1.6–22.4，比矩形宽。改为 r=8.2，按包围盒居中 |
| Cube | 需重绘 | 隐藏棱 1 宽、dash 1.5 2，在顶点和交点上直接压着实线；隐藏顶点离可见棱太近 |
| Cuboid | 需重绘 | 同上；外形有 60° 尖角 |
| Cylinder | 需重绘 | 隐藏的后半弧端点压在侧边和前弧上 |
| Cone | 需重绘 | 同上 |
| Prism | 需重绘 | 隐藏顶点 (11,15) 几乎落在前三角形的右边上（距离 0.16），虚线和实线混成一团 |
| Pyramid | 需重绘 | 隐藏顶点的虚线和实线棱交叉、重叠 |
| Polyline | 合规 | 开放线条，2.6 圆端点 |
| Arrow, Rotate | 合规 | 开放线条 |
| Outline, Fill | 合规 | 样式按钮 |
| Solid, Dashed, Dotted, DashDot | 合规 | 线型按钮，dasharray 本身就是内容 |
| Angle, Parallel, Perpendicular, NumberLine, Grid | 合规 | 禁用占位类别。主线 2.6，辅助记号 1.3，属于可接受的次级层级。Grid 线较密，可选后续优化 |
| Cartesian | 合规（备注） | 坐标轴 2.6，箭头只有 1.3，与 NumberLine 的 2.6 箭头不一致。禁用项，暂不改 |
| CoordinatePoint | 合规（备注） | 投影虚线 0.85、dash 1.5 2，和新的隐藏棱虚线规格不一致。禁用项，暂不改 |
| Parabola, Reciprocal, Hyperbola, Sine, Cosine, Tangent, Exponential, Logarithmic | 合规（备注） | 函数族：曲线 2.6、坐标轴 0.85、渐近线 0.65 虚线。曲线开放，不加填充是对的。0.85/0.65 在 1.0 倍（28 DIP）下不到 1px，很淡。都是禁用项，是否统一到新虚线规格需另行决定 |

## 立体图标的绘制方法（草稿）

- 可见棱 2.6，圆端点、圆连接。
- 隐藏棱 1.4，平端点，`dasharray` 以 `2 2.4` 为基准，按每段长度缩放，保证两端都是实段。
- 每条隐藏棱按"与任一可见棱中线的距离 ≥ 1.3 + gap"裁剪，在交点和顶点处自动断开，只保留净空段。
- 如果一段只容得下一个短划：从隐藏顶点一侧开始画一段 2.0；其他情况居中放置。这样不会读成实线。
- Cube / Cuboid 用斜二测，前面正对。比例按"三条隐藏棱长度均衡"求解：Cube 边长 11.7、纵深 5.3；Cuboid 为 12.5×10、纵深 4.5。
  - 前面不加填充。填充层叠在隐藏顶点的虚线后面，在 24px 下会读成"图片"图标。
- Prism：横放三棱柱，前三角面加 0.24 填充，纵深偏短，避免读成橡皮或铅笔。
  - gap 1.5 时，底面后棱 AA' 会被完全让开而消失；gap 0.8 时三条隐藏棱都可见。
- Pyramid：保留现有四棱锥轮廓，比例经 `search_natural.py` 搜索得到。
- Cylinder：顶面椭圆加 0.24 填充。Cone：不填充。两者的隐藏后半弧在端点处让开。
- 搜索脚本放在 `tools/search_*.py`，只用于推导参数，不进入产品。

## 待用户确认

1. **隐藏棱间隙**。
   - 按 PRD 的 1.5（边到边）裁剪后，24px 下隐藏棱只剩隐藏顶点附近的短"Y"，Prism 的一条棱会消失。
   - 0.8 能保留更多结构。
   - 预览中立体图标并排给出了两列。
2. **基准族填充 0.16 → 0.24**。这 6 个是用户认可的图标，改透明度会让它们与矩形一致，但会比现在略重。
3. **StraightLine 3.2 → 2.6**。原来的 3.2 可能是有意加粗，用来平衡单线的视觉重量。
4. **立体图标的填充**：Cube/Cuboid 不填；Prism 填前三角面；Cylinder 填顶面；Cone/Pyramid 不填。
5. **Prism / Pyramid 的形态与画布不同**。画布默认是直立的三棱柱、三棱锥（`shape_presets.inl`，sides=3）。图标用横放三棱柱和四棱锥，因为直立三棱柱在 24px 下读成"房子"，正面三棱锥读成"警示三角"。
6. **正多边形 3–12 的轮播**：奇数边按包围盒居中后，中心点在轮播帧之间会上下移动最多约 2.4。
7. **函数族和坐标族的细线规格**是否统一，本轮没有改。

## 最终决定（2026-11-05 用户经协调者确认）

1. 6 个立体图标采用 **gap 0.8** 版本（`alt-gap08/`）。
2. 基准族填充统一为 **0.24**：Ellipse、RoundedRectangle、Triangle、Parallelogram、Rhombus、Trapezoid、OutlineFill。
3. **barShapeStraightLine 保持 3.2，不替换**。审计表中该项改记为"保留（用户决定）"。
4. Prism 用横放三棱柱、Pyramid 用四棱锥的草稿形态，接受与画布默认形态不同。
5. 其余草稿选择保留：CircleBounds 用角标；奇数边正多边形按包围盒居中（中心点在轮播帧间会移动）；Cube/Cuboid 不填充；Prism 填前三角面；Cylinder 填顶面；Cone/Pyramid 不填充。
6. 函数族和坐标族细线本轮不改。

### 已替换的文件（28 个，来源 `Build/shape-ux-icons/alt-gap08/`，按原文件保持 CRLF、无 BOM）

barShapeArc, barShapeCircleBounds, barShapeCircleCenter, barShapeCone, barShapeCube, barShapeCuboid, barShapeCylinder, barShapeEllipse, barShapeOutlineFill, barShapeParallelogram, barShapePolygon, barShapePolygon3, barShapePolygon4, barShapePolygon5, barShapePolygon6, barShapePolygon7, barShapePolygon8, barShapePolygon9, barShapePolygon10, barShapePolygon11, barShapePolygon12, barShapePrism, barShapePyramid, barShapeRhombus, barShapeRoundedRectangle, barShapeSector, barShapeTrapezoid, barShapeTriangle（均为 `.svg`）

资源名、rc、vcxproj 都没有变化（spec bar-icons 的 Base 情况）。

## 验证状态

- 草稿 XML 都能解析。只使用两个占位色，`fill-opacity` 只有 0.24，没有其他 opacity。
- 预览页已生成，`msedge --headless` 截图完成。
- 用户确认后，已把 28 个文件替换进 `Inkeys/src/UI`。完整构建和 `--ui3-geometry-attribute-test` 尚未运行，由主会话执行，见 `../validation.md`。
- 浏览器渲染与 lunasvg 可能有细微差异：草稿只用了 path/rect/circle/ellipse、fill-opacity 和 stroke-dasharray，这些在现有资源中都已使用过。

# 图形图标风格统一

## Goal

把主栏图形类别的 SVG 图标统一成同一种风格。用户认可最初的直线、矩形等图标；后来加入的多边形、立体等图标风格不一致，立体图标的实线与虚线混在一起，交点处很尖锐。

## 已确认决策（2026-11-05 用户）

- I1 只改主栏图标（`Inkeys/src/UI/barShape*.svg`）。画布上立体图形的渲染保持现状。

## 现状

- 基准风格：P2 图标，即直线、矩形、椭圆、三角形、平行四边形、菱形、梯形等。
  - 24×24 画布；
  - 主描边 2.6，圆端点、圆连接；
  - 封闭图形另加一层 `rgba(9,0,2,0)` 24% 填充；
  - 使用主题语义占位色（spec `rendering-and-ui.md:2473`）。
- 只有 8 个图标带 24% 填充：Ellipse、OutlineFill、Parallelogram、Rectangle、Rhombus、RoundedRectangle、Trapezoid、Triangle。后加的封闭图形没有填充，例如 Polygon、正多边形 3–12、Sector。
- 立体图标的问题（例如 `barShapeCuboid.svg`、`barShapeCube.svg`、`barShapeCylinder.svg`）：
  - 实线 2.6，虚线 `stroke-width=1`、`dasharray 1.5 2`；
  - 虚线和实线在顶点处重叠，24px 下看起来很杂乱；
  - 外轮廓的折角很尖。

## Requirements

- R1 先审计全部 `barShape*.svg`，按基准风格分为「合规」和「需重绘」两组，列出清单。
- R2 重绘时遵循基准风格：
  - 描边 2.6，圆端点、圆连接；
  - 封闭平面图形（多边形、正多边形 3–12、扇形等）加 24% 填充；开放线条不加填充，例如折线、圆弧、函数曲线；
  - 视觉重心和占满程度与矩形一致，在 24×24 中留出相同的边距。
- R3 立体图标（长方体、正方体、圆柱、圆锥、棱柱、棱锥）：
  - 可见棱用 2.6 实线；
  - 隐藏棱用更细的虚线，暂定 1.4，dash 和 gap 加大，例如 2 2.4；
  - 虚线在与实线相交处让开约 1.5，不压在实线上；
  - 几何适当简化，避免小尺寸下线条过密；
  - 必要时只在正面保留 24% 填充，帮助辨认体积。
- R4 不改资源名，不改代码、rc 或 vcxproj（属于 spec bar-icons 中的 Base 情况）。只能用主题占位色，不能写死颜色。
- R5 出一份预览页（主栏实际尺寸 1.0/1.5/2.0 倍率 × 深浅主题，选中与未选中），用户确认后再替换资源。

## Acceptance Criteria

- [ ] 审计清单写入 `research/icon-audit.md`。
- [ ] 预览页经用户确认。
- [ ] `--ui3-geometry-attribute-test` 中的真实 SVG 像素和非空检查通过；深浅主题下选中时的变色正常。
- [ ] 完整构建 0 error。

## Dependencies

- 与 toolbar-category 没有文件冲突（那边只改 `Bar.GeometryAttribute.cpp`，不改 SVG），可以和其他子任务并行，也可以穿插进行。

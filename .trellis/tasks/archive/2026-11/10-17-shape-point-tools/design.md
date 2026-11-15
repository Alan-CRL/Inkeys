# 逐点折线与任意多边形 技术设计

复用P3的shared ConstructionSession/EditSession、runtime journal/Bridge快照与UI3提示。依赖P3提示/跨笔会话。Polyline>=2，Polygon>=3且简单有效，Up存raw点，Complete闭合。
构造、样式、投影、控制点、兼容和取消规则严格采用 ../10-17-shape-phase3-selection/prd.md；不独立创建渲染/历史/窗口框架。

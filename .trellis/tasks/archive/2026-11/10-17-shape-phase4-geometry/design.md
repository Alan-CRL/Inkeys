# P4多阶段曲线与空间几何 技术设计

复用P3的shared ConstructionSession/EditSession、runtime journal/Bridge快照与UI3提示。依赖P3编辑和提示。只Arc/Sector和6空间形；Angle/Parallel/Perpendicular后置。
构造、样式、投影、控制点、兼容和取消规则严格采用 ../10-17-shape-phase3-selection/prd.md；不独立创建渲染/历史/窗口框架。

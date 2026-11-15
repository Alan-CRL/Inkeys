# 第二轮共享几何与持久化

- 新增 ToolSpec.constraintVertex / rightSolid；三角形首次约束按顶点最小位移选择底边，重复等腰动作循环顶角，编辑约束与校验使用同一身份。
- 新 SolidConstruction.oblique 显式区分新斜二测与旧投影；baseSize、baseRotation、projectionAngle、projectionScale、offset 为构造真值。
- 新建统一底面预览，Cube 高度绝对值等于底边，完成后顶点、底面姿态与正形约束经同一共享编辑路径重建。
- 投影视线是二维映射的核；面朝向决定边虚实。圆柱/圆锥轮廓连续弧合并，避免每个细分弦重置虚线。
- 新参数写 v6；旧 v1–v5 参数默认 oblique=false，不自动重投影。正形按钮首次操作旧棱柱/棱锥时精确保留旧底面投影参数后升级。
- v6 同时支持闭合虚线的标准降级结构（root 负责渲染和标准导出）。三角形 apex0 等旧语义可继续最低版本。
- 增加共享核心构造、句柄、等腰轮换、正形回正、圆形隐藏弧和 v6 参数往返测试。构建与执行由 root 集成；尚不声称测试通过。
- 保留 UTF-8 BOM 与 CRLF；局部 diff --check 通过。没有 GUI、commit 或 push。

# 第二轮：构造会话与生产输入

## 实现边界

- ConstructionSession 增加已确认顶点优先命中，拖动保持 Down 相对偏移；Preview/Confirm 不再把该接触当成新顶点。
- 点工具撤回/恢复保存完整顶点快照，覆盖加点与调整；取消单次顶点接触恢复原顶点并允许继续绘制。
- 点工具首个 Down 即提供可绘制点，首拖在 Up 前提供第一条边。无效候选保留之前可绘制路径。
- 新物理接触通过 PrepareRuntimeConstructionContact 继承步骤间预览和 RenderData，避免 Down 到首个 Move 之间 active runtime 空几何导致 L0 重建空帧。
- 构造快照发布 preview object、bounds 和已确认顶点 handles；UI 只画这些手柄，输入仍归画布物理接触路径，避免双重确认。
- SetRightSolid 动作复用属性候选和 Replace 事务；新立体首步/最终构造调用共享几何 helper。

## 验证

生产无窗口回归加入首点 Down 可绘制、首边 Move 可绘制、已有点调整不加点、不写文档、撤回/恢复、取消还原、调整后继续加点与加点撤回。完整构建及执行由主代理集成运行，未启动 GUI。

文件保留原 UTF-8 BOM 状态与 CRLF。无 commit、push 或工程配置改动。

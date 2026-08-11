# UI3 光影渲染极致性能优化：执行计划

## 1. 建立基线

- [ ] 在现有 Debug 模式加入低开销阶段计时与计数：动画/状态、光影准备、D2D 记录、GetDC+ULW、总帧、CommandList、Gaussian、设备资源创建。
- [ ] 记录关闭光影与“光影 + 动态光影”下属性栏展开/收起的同机基线。

## 2. 共享设备基础

- [x] 将 WARP 专用命名重构为 UI3 共享设备 epoch，默认仍创建 WARP。
- [x] 实现 Feature Level 11.1→11.0 兼容重试和 Hardware 创建失败保留当前 epoch。
- [x] 实现完整帧串行租约、交互优先和装饰帧非阻塞获取。
- [x] Bar 资源绑定设备代号，代号变化时重建 DeviceContext、目标位图与全部缓存。
- [x] 预留双管线 prepare/commit 接口，不接入欢迎页。

## 3. 光影热路径

- [x] 持久化渐变停止集合、径向 Brush 和纯色 Brush，移除逐帧 cache clear。
- [x] 实现颜色无关的 A8 预模糊 Alpha 遮罩缓存；A8/Effect 失败时会话级停用柔光。
- [x] 固定尺寸色块/按钮共享遮罩；超椭圆复用量化后的变换遮罩。
- [x] 为连续宽高动画的圆角栏实现角/边九宫格遮罩。
- [x] 将柔光改为 FillOpacityMask，清晰边线保持实时绘制。
- [x] 加入光源半径、控件边界和 dirty rect 的前置剔除。
- [x] 删除逐控件 CommandList/Gaussian 热路径；兼容失败不退回逐帧实时模糊。

## 4. 局部绘制与提交

- [x] 在 BeginDraw 前计算上一帧/下一帧保守边界并扩展光影余量。
- [x] 使用 dirty clip 约束透明 Clear，避免触碰其余全屏位图。
- [x] 在 GetDC 前 Pop 所有 Clip/Layer，并复用 dirty rect 进行 ULW 提交。
- [x] 删除显式 Flush；补齐 EndDraw/target 重建和遮罩降级处理。

## 5. 绘图静默

- [x] 导出配对的 `NotifyCanvasDrawingStarted/Ended`。
- [x] 在 MultiFingerDrawing 入口加入 RAII 守卫，移除派发前单向通知。
- [x] 实现活动笔画计数、150ms 防抖和窗口线程 quiet 状态。
- [x] quiet 期间停止动态光和装饰帧，必要交互直接提交目标状态的一帧。

## 6. 验证顺序

- [x] 运行静态检查与 `git diff --check`。
- [x] 使用 ARM64 MSBuild 构建 `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64`，超时至少 5 分钟。
- [ ] 执行遮罩、dirty rect、租约、设备代号和多指计数相关测试。
- [ ] 重复性能基准并对照 PRD 门槛；逐阶段记录收益和回归。
- [ ] 人工检查 100/150/200% 缩放、边角光源、展开中落笔及绘图中点击 Bar。
- [ ] 在可用的 Windows 7 SP1 + KB2670838 环境执行兼容验证；若环境暂不可用，保持 WARP 默认且不得以 Hardware 掩盖未验证结果。

## 7. 用户验收记录

- [x] 2026-07-26：实际验证绘制属性窗口展开过程，帧率提升明显。
- [ ] 其他视觉、兼容性和多窗口串行场景待窗口拆分完成后继续接入与优化。

## 回滚点

1. 共享设备 epoch 与串行租约。
2. 绘图开始/结束配对和 quiet 状态。
3. 光影遮罩缓存。
4. dirty rect 局部绘制。
5. Hardware 准备/提交接口。

# UI3 光影渲染极致性能优化

## Goal

在保持 UI3 边缘光影整体观感、交互和 60 FPS 动画节奏的前提下，消除属性窗口展开等高频动画场景中的严重掉帧，使开启“光影 + 动态光影”后的主栏渲染成本尽可能接近关闭光影。默认后端继续使用 WARP，为画布绘制让出 GPU 并降低能耗。

## Background

- 当前 UI3 Bar 使用 D3D11 WARP、D2D1.1 Effect 和全屏 GDI-compatible 位图。
- 单帧已经只有一个 `BeginDraw/EndDraw` 与一个 `UpdateLayeredWindowIndirect`，但每个 PointLight 控件仍会创建 CommandList 并执行 Gaussian Blur。
- 属性窗口展开时约有 15–16 个 PointLight 控件；动态光影开启后，逐控件模糊成为主要热路径。
- 现有 `prcDirty` 只约束分层窗口提交，D2D 仍会全屏清除和遍历绘制。
- 之前尝试缓存快速变化的最终光影状态、冻结布局或延迟恢复，已因状态和动画问题被放弃，本任务不得恢复该方案。

## Requirements

1. 最低支持 Windows 7 SP1 + KB2670838；不得依赖 DirectComposition、WARP 共享表面或 Windows 8 专属行为。
2. UI3 默认使用同一个 D3D11.1/D2D1.1 WARP 设备。当前 Bar 和未来 PptBar、Setting、白板必须能够在共享设备上串行绘制。
3. 缓存只能保存与光源位置和颜色无关的几何、Alpha 遮罩、画刷与其他设备资源；不得缓存最终合成光影状态。
4. 固定尺寸控件应复用预模糊遮罩；连续改变宽高的圆角栏应避免逐帧重新运行 Gaussian Blur。
5. 稳态渲染不得再为每个控件创建 CommandList 或运行 Gaussian Blur；兼容失败路径也必须合并为帧级操作。
6. 实现真实的局部清除、D2D Clip、控件边界剔除和分层窗口脏区提交；不得继续每帧全屏 `Clear`。
7. 删除冗余显式 `Flush`，并遵守 Windows 7 上 `GetDC` 必须位于 `BeginDraw/EndDraw` 之间且不能位于 Clip/Layer 内的约束。
8. 绘图期间进入“保留交互降载”模式：
   - 保持主栏可见且点击、选择和必要状态变化可响应。
   - 暂停动态光源、Raw Input、悬停及其他纯装饰连续帧。
   - 必要状态变化最多提交一帧，不播放连续装饰动画。
   - 多指绘图必须以活动笔画计数管理，最后一笔结束 150ms 后恢复。
9. 共享设备管理层必须使用设备代号隔离 DeviceContext、目标位图、画刷、Effect 和遮罩缓存，并为未来欢迎窗口预留 WARP → Hardware 的双管线帧边界切换能力。
10. 本任务不实现欢迎窗口、不改变动态光影的默认值，也不默认启用 Hardware。

## Acceptance Criteria

- [ ] WARP 同机同分辨率预热后，连续执行至少 30 次属性窗口展开/收起；“光影 + 动态光影”相较全部关闭的额外 UI3 帧耗时中位数不超过 0.5ms，P95 不超过 1ms。
- [ ] 60 FPS 场景中超过 16.67ms 的帧不超过 1%，且没有连续掉帧。
- [ ] 预热后的稳态帧中，设备资源创建、CommandList 创建和实时 Gaussian Blur 次数均为 0。
- [ ] 100%、150%、200% 缩放以及光源位于边、角、控件内部时无可见接缝、闪烁、拖影或光影跳变；允许肉眼不可辨的像素级差异。
- [ ] 多指交叠、快速连续落笔、唯一早退路径、绘图时点击 Bar、属性栏展开中落笔均能严格配对开始/结束通知并正确恢复。
- [ ] 所有 UI3 绘制租约严格串行；白板/交互优先时 Bar 装饰帧可跳过，必要交互帧不会永久饥饿。
- [ ] 设备代号变化会完整重建设备相关资源；Hardware 准备失败时旧 WARP 管线继续工作且无空白帧。
- [ ] `InkeysRepo.sln` 的 `Debug | ARM64` 使用 ARM64 MSBuild 编译通过，`git diff --check` 通过。
- [ ] Windows 7 SP1 + KB2670838 上验证 WARP Feature Level 11.0、D2D1.1 Effect、GDI Interop 与设备创建回退路径。

## Out of Scope

- 首次启动欢迎窗口及“华丽界面”产品逻辑。
- 默认关闭动态光源的设置迁移。
- PptBar、Setting 和白板本身的绘制实现。
- 默认切换为 Hardware 或根据帧率自动反复切换后端。

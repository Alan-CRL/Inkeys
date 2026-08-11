# UI3 光影渲染极致性能优化：技术设计

## 1. 共享设备边界

现有 D2D 工厂与 WARP 设备改为中性的 UI3 共享设备管理层。管理层拥有当前后端、D3D11/D2D1.1 设备、Feature Level、设备代号和全帧串行锁；每个 UI3 客户端仍持有自己的 DeviceContext、目标位图与设备资源。

渲染租约覆盖客户端完整的绘制和提交区间，避免不同 DeviceContext 的命令在共享 WARP 设备上交错。交互帧阻塞取得租约；光标、悬停和光影等装饰帧使用非阻塞租约，在画布活动或有交互请求等待时直接跳过。

设备创建使用 `D3D11_CREATE_DEVICE_BGRA_SUPPORT`，保留 WARP 内部线程优化。先请求 Feature Level 11.1/11.0；`E_INVALIDARG` 时移除 11.1 重试。Windows 7 Platform Update 下允许最终为 Feature Level 11.0，并通过 QueryInterface 获取可用的 D3D11.1 接口。

## 2. 设备代号与未来热切换

共享设备以不可变 epoch 发布，每个 epoch 包含后端、代号与设备对象。客户端的 DeviceContext、目标位图、Brush、Effect 和遮罩缓存记录所属代号；代号不匹配时必须整体释放并重建。

未来欢迎页的 Hardware 切换使用双管线：

1. 旧 WARP epoch 继续显示。
2. 后台准备 Hardware D3D/D2D 设备。
3. 帧边界停止发放新租约并等待当前租约退出。
4. 发布新 epoch；客户端在下一帧重建设备资源并完成全量绘制。
5. 新 epoch 首帧成功后释放旧 epoch；失败则保持 WARP。

本任务只实现 epoch、串行租约和准备/提交边界，不接入欢迎页。

## 3. 光影数据流

光影拆为三层：

1. 基础灰色边框：继续直接绘制。
2. 柔光：使用预先生成的颜色无关 Alpha 遮罩，每帧用当前径向渐变 Brush 执行 `FillOpacityMask`。
3. 清晰高光边线：继续用当前光源 Brush 直接绘制 1px 轮廓。

固定几何使用紧凑完整遮罩；同尺寸色块和按钮共享。连续改变宽高的圆角栏使用整数像素对齐的角/边分片遮罩，四角保持原始比例，边段仅沿长度方向拉伸。遮罩使用 `DXGI_FORMAT_A8_UNORM`；这是 `FillOpacityMask` 的原生缓存格式。若 Windows 7 Platform Update/WARP 无法创建或写入 A8 目标，则本会话停用柔光遮罩，只保留基础边框和清晰高光线，不能退回逐控件实时 Gaussian。

遮罩在创建时执行一次 BALANCED Gaussian Blur 并固化到位图。专用遮罩 DeviceContext 用两个独立 `BeginDraw/EndDraw` 区间依次写源遮罩和模糊输出，不能在同一个绘制区间把仍绑定为 Target 的位图作为 Effect 输入。缓存键包括设备代号、几何类型、物理尺寸或分片规格、圆角/超椭圆参数、描边宽度、缩放和 sigma；颜色、光源坐标、光照强度不进入键。圆角和几何遮罩各最多 24 项，渐变画刷最多 32 项，设备代号变化时整体清空。

## 4. 局部渲染

动画推进后、`BeginDraw` 前，由主按钮、主栏、绘制属性栏三个根区域计算下一帧保守边界，并扩展最大描边、Gaussian 半径和抗锯齿余量。该边界与上一帧已提交边界取并集作为 dirty rect。

绘制时：

1. Push dirty clip。
2. 在 dirty clip 内 Clear 为透明色。
3. 恢复 SOURCE_OVER，绘制与 dirty rect 相交的控件。
4. Pop clip。
5. 调用 GetDC、UpdateLayeredWindowIndirect、ReleaseDC。
6. EndDraw。

不再调用显式 Flush。Debug 全屏覆盖或边界计算异常时退化为全屏 dirty rect，优先保证无残影。

## 5. 绘图静默状态

Draw2 在 `MultiFingerDrawing` 入口创建 RAII 活动守卫；构造时通知开始，析构时通知结束，覆盖 Canvas 为空的早退。

Bar 侧使用原子活动笔画数和窗口线程状态机：

- 首笔 `0→1`：取消恢复计时器，进入 quiet，注销 Raw Input，并请求一帧移除动态光。
- quiet 期间：装饰帧不获取渲染租约；必要状态更新直接完成到目标值并提交一帧。
- 末笔 `1→0`：启动 150ms 计时器。
- 计时到期且计数仍为 0：退出 quiet；不自动恢复旧光标光源，等待真实离开/重新进入。

未来白板通过共享设备的高优先级租约自然抑制 Bar 装饰帧；当前 Draw2 仍通过活动笔画状态提供同等降载。

## 6. 失败与回滚

- 遮罩创建失败：该帧保留基础边框和清晰光线；单次记录日志，不得每帧重试。
- A8 不支持：本会话停用柔光遮罩，保留基础边框和清晰高光线。
- `EndDraw` 返回设备丢失：失效当前 epoch，重建所选后端；Hardware 失败则本次会话回退 WARP。
- dirty rect 预测不可信：全屏重绘，不允许残影。
- 每个阶段均保持可独立回滚：共享设备边界、静默通知、资源缓存和局部绘制分别验证后再继续。

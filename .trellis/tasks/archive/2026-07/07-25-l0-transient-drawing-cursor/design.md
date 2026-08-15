# L0 瞬态绘制光标设计

## Architecture

光标属于 L0 帧的瞬态视觉，但不写入共享 `layerL0`。每次呈现先把 dirty 区域从 `L2 + L1 + L0` 合成到 backbuffer，再以双源 premultiplied Alpha 混合绘制光标。这样旧位置可由正常图层重建清除，contact 完成时也不可能把光标 resolve 到 L2。

现有 ink HLSL 增加三种 cursor shape。顶点阶段生成围绕中心和半尺寸的 quad，像素阶段用解析 SDF 计算圆、矩形、圆环和圆头抓手线覆盖率。`InkRenderer::DrawTransientDrawingCursor` 只上传小型常量/primitive 并直接绘制到 backbuffer，不分配尺寸相关资源。

## Input And State

- RTS sink 发布包含 X/Y/QPC、Hover/Contact、倒转状态的 Pen 样本。`InAirPackets` 校验包布局并只解码最后一个包。
- `WM_POINTERENTER/UPDATE` 动态调用 `GetPointerInfo/GetPointerPenInfo` 直接发布 Pen Hover 坐标，补足部分驱动在 Down 前不发 InAir packet 的窗口。
- WindowController 分别保存 Pen 和 Mouse 的无锁一致快照，并维护 `Unknown/Pen/Mouse/Touch` pointer authority。
- Pen/Touch 离开时清除当前可见样本，但保留最后设备 authority；因此陈旧 Mouse 快照不会重新显示，直到新的非 promoted Mouse 消息明确接管。
- 非 promoted `WM_MOUSEMOVE` 更新 Mouse 样本；`TrackMouseEvent` 和 `WM_MOUSELEAVE` 负责清除窗口外状态。
- 样本更新通过现有 control wake 合并；系统光标刷新继续使用窗口线程的单个私有消息，RTS 回调不直接调用 `SetCursor`。
- DrawingController 按 authority、设备状态、当前选择工具和活动 contact 锁定工具解析 Pen/Mouse 主光标；同时遍历活动 Touch eraser runtime，为每个 contact 生成独立的不透明 `DrawingCursorVisual`。

## Frame Lifecycle

DrawingController 保存上一帧全部 cursor 的联合 bounds。任何样本、工具、触点集合或可见性变化都把旧 bounds 与当前全部 visual bounds 合入 `frameDirty`；正常图层合成后逐枚绘制当前 cursor。全量呈现也执行同样的最后一步。cursor 不写入 ActiveStroke、稳定前缀、预测、metrics 或 reconnect 状态。

Hover-only 更新取最新样本并按当前 120Hz profile 合并；静止后重新进入阻塞等待。Down、resize、clear、exit 等控制请求可以打断 cursor pacing。

## Compatibility And Failure

- Windows 8+ Pointer API 决定 Pen/Mouse/Touch authority；旧系统由 RTS Pen 状态和真实 Mouse 消息回退。Touch cursor 直接来自已经进入绘制线程的活动 contact，不依赖全局 pointer authority，因此支持多指。
- renderer 未就绪时不隐藏系统光标；应用正常运行后只使用 `IDC_ARROW` 或 `SetCursor(nullptr)`。
- 删除 `CreateIconIndirect` 路径，不在 GPU 绘制失败时退回已知颜色错误的彩色硬件光标。
- 当前 Qualcomm `UlwDirtyRect` 和其他透明 presenter 都读取同一 premultiplied backbuffer，因此无需 presenter 专用分支。

## Trade-offs

直接写共享 `layerL0` 会污染 L2 resolve，并且无法在同一 MAX/MIN operator layer 中可靠表达多颜色光标覆盖，所以选择 backbuffer 最终叠加。该方案可能比硬件光标增加至多一帧延迟，但允许动态尺寸、正确颜色和窗口级生命周期。

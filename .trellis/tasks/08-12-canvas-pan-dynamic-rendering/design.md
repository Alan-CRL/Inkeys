# Technical Design

## Boundaries

- 新增纯 CPU `canvas_navigation` 模块，拥有触摸批次状态、视口速度/惯性、范围保护、动态 tile 排序与帧预算计算，便于无窗口单元测试。
- `WindowController` 只发布带 DIP delta 的键盘命令；RTS 发布 Pen proximity 原子状态；`DrawingController` 在绘制线程消费并修改文档视口、D3D 层和手势状态。
- 所有 Stored point、history footprint 和 composition key 保持 Canvas-local；输入进入 modeler 前执行 `canvas = screen + viewportOrigin`，绘制到尺寸相关层时执行反向偏移。

## Rendering

- composition tile 仍是 256px Canvas-local operator cache；restore/prime 增加 viewport origin，把 Canvas tile 映射到屏幕目标矩形。
- L2 表示当前视口的权威清晰稳定层。视口改变时清空并优先恢复可见 tile；热前像清空，composition cache 保留。
- renderer 维护稳定 L2 快照纹理和兜底纹理。平移前从完全清晰 L2 刷新快照；视口变化后只把快照的真实交集重投影到 backbuffer，并用定向多采样实现 0..12 DIP 模糊。
- 每帧先恢复当前可见缺失 tile，再按 150ms 速度预测范围预热 composition node；每个 tile 后检查输入。恢复预算为目标帧剩余时间减 1ms，最多 4ms，并以 tile EWMA 估算数量。
- 热前像改为 screen-local 整数矩形副本，Canvas tile 只用于确定受影响范围；捕获/恢复仍要求 viewport 完全一致，但不要求 viewport 自身为整数。
- `CanvasRuntimeHistory` 维护可见 composition tile 的引用计数/索引，平移规划按当前/预测 tile 范围查询，不每帧拼接整页所有 item tile。Canvas-local footprint 不随窗口 Resize 改变。
- composition 单 tile 重建从 Stored Stroke 提取与 tile 扩展范围相交的连续点段/解析 Shape；只上传局部几何，并保留相邻连接点保证胶囊、荧光笔 sweep 和擦除边界连续。

## Input State

- 空闲首指按当前工具开始 runtime；第二指及时落下时取消所有 Touch runtime 和 Laser 瞬态并重建剩余 contact 层。
- 惯性首指仅登记候选，不创建 runtime。180ms 内第二指进入接续平移；超时后标记该 contact suppressed-until-up，其他迟到 contact 可按工具绘制。
- 平移接管后的中心变化直接驱动位移；旧惯性在 120ms 内衰减叠加。最后触点抬起后使用采样速度启动惯性。
- Pen hover 只改变惯性阻尼；Pen/Mouse Down 立即结束手势并清零速度，仍落下的 gesture Touch 保持 suppressed-until-up。
- 调低 Windows inertia 的期望减速度并同步 CPU fallback，使常规甩动具有接近触摸网页的滑行距离；hover/候选超时的加强制动相对比例保持明显。
- contact dequeue 前读取的 Pen mailbox 只负责提前刹停；真实 Pen Down 出队时仍在同一循环创建 runtime，并以刹停后的固定 viewport 变换 Down 点。
- 平移速度只由真实 Touch Move 及其 QPC 更新，渲染空帧不得覆盖；最后 Up 前锁存 Windows 速度并限制为最近 `100ms` 输入。Windows inertia 的旧 Completed 必须重置，创建/启动/步进失败时由同参数 CPU 线性惯性接续。

## Compatibility

- 首选 Windows manipulation/inertia COM；创建失败时导航模块继续直接跟手并禁用自动惯性。
- 不改变持久化 Stroke 格式和 `InkHistoryRasterKey`；视口只由每个 `InkCanvas` 保存。
- 失败时清空兜底资源并回到逐 tile 权威重建，不允许旧位图进入 L2。

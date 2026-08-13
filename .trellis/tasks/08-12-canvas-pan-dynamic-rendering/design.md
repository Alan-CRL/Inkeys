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
- 平移接管后的中心变化直接驱动位移；旧惯性在 120ms 内衰减叠加。QPC 是唯一时间源；固定容量 `24` 的 Move 样本在约 `100ms` 窗口内对累计中心位移做线性拟合。零位移包、Up 终态和渲染空帧不得进入估速；最后 Up 只补齐最终中心位移，并按输入 QPC 时效启动惯性。
- 活动 Touch 跟手平移期间到达的 Pen contact 锁存为 suppressed-until-up，不结束手势、不改变速度，也不能在随后惯性阶段补画；同一锁存状态由绘制线程发布给窗口线程，必须同时清除接触光标、停止/拒绝触觉绑定，并保持到对应 `WM_POINTERUP`/`WM_POINTERLEAVE` 或 RTS 终态。Mouse Down 仍可立即抢占。惯性阶段的 Pen hover 改变阻尼，抬笔后重新产生的 Pen Down 立即清零速度并绘制。
- 应用层惯性的普通线性减速度为 `4000 DIP/s^2`，形成克制的网页级滑行；Pen hover/候选超时仍使用 `12000 DIP/s^2` 加强制动。
- contact dequeue 前读取 Pen mailbox：活动 Touch 跟手时只锁存本次 Pen suppression；惯性阶段才提前刹停。可抢占的真实 Pen Down 出队时仍在同一循环创建 runtime，并以刹停后的固定 viewport 变换 Down 点。
- 惯性中的新双指先保存应用层残余速度并停止旧惯性步进，再由当前触点立即开始新手势；残余速度在 `120ms` 内混入跟手位移，同向加速和反向制动都由同一个应用层状态机拥有。
- 触点拓扑变化清空旧拟合窗口并以当前中心重建零点，同时保留此前锁存速度。首指 Down 开始的新零 Touch 批次显式重置旧批次的中断/超时资格。
- 视口硬限位以 `double` 候选原点直接比较 `+/-1048576 DIP`，只在候选真实越界时报告 clamp 并清零对应轴；不得通过 float 应用前后的微小差值反推撞边。
- Pen mailbox 保留活动 Touch 平移 suppression 和惯性 hover 制动；Mouse mailbox 不作为导航抢占真值，避免 Touch-to-Mouse 提升污染。只有出队的真实 Mouse contact 才可中断导航。

## Compatibility

- 导航不依赖 `IManipulationProcessor`、`IInertiaProcessor` 或 COM 可用性；`CanvasPanMotionState` 是手势、估速和线性惯性的唯一权威，所有受支持 Windows 版本走同一实现和参数。
- 不改变持久化 Stroke 格式和 `InkHistoryRasterKey`；视口只由每个 `InkCanvas` 保存。
- 失败时清空兜底资源并回到逐 tile 权威重建，不允许旧位图进入 L2。
- 主窗口创建扩展样式当前固定包含 `WS_EX_TOPMOST`，使用 `SW_SHOWNORMAL` 允许独立测试宿主取得键盘焦点；接入 Inkeys 后再恢复 NOACTIVATE。窗口高度为主显示器高度减 1。

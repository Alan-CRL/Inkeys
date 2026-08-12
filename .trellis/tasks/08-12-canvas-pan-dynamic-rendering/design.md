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

## Input State

- 空闲首指按当前工具开始 runtime；第二指及时落下时取消所有 Touch runtime 和 Laser 瞬态并重建剩余 contact 层。
- 惯性首指仅登记候选，不创建 runtime。180ms 内第二指进入接续平移；超时后标记该 contact suppressed-until-up，其他迟到 contact 可按工具绘制。
- 平移接管后的中心变化直接驱动位移；旧惯性在 120ms 内衰减叠加。最后触点抬起后使用采样速度启动惯性。
- Pen hover 只改变惯性阻尼；Pen/Mouse Down 立即结束手势并清零速度，仍落下的 gesture Touch 保持 suppressed-until-up。

## Compatibility

- 首选 Windows manipulation/inertia COM；创建失败时导航模块继续直接跟手并禁用自动惯性。
- 不改变持久化 Stroke 格式和 `InkHistoryRasterKey`；视口只由每个 `InkCanvas` 保存。
- 失败时清空兜底资源并回到逐 tile 权威重建，不允许旧位图进入 L2。

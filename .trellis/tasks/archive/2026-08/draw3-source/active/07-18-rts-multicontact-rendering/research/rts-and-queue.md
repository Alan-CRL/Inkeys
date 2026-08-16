# RTS 与阻塞队列研究摘要

## Existing project

- 当前入口在 `main.cpp` 轮询 Win32 mouse message，并调用单笔 `DrawingController::DrawMouseStroke()`。
- 当前渲染使用 L2（固定已完成墨迹）、L1（稳定前缀）和 L0（实时尾部/预测）。D3D/D2D 由绘制线程独占。
- 当前 ink-stroke-modeler 已支持 `kDown`、`kMove`、`kUp`，并已有预测、速度模拟压感和最终可见尾部合并规则。
- 当前 vcpkg manifest 已固定 concurrentqueue 1.0.4，且自定义 triplet/install 路径已能完成 `Release|ARM64` 基线构建，不应调整。

## RTS constraints

- Microsoft 对 StylusInput 同步插件的建议是保持回调轻量；因此同步插件仅解析 packet 并发布快照，不执行模型、渲染、分配或热路径 COM 查询。
- `IRealTimeStylus3` 提供多点启用；本任务把该接口作为必需能力，不实现失败回退。
- packet 元数据按 tablet context 缓存。Down/Up 为单 packet；Packets 批次只消费最后一个完整 packet，符合“允许丢 Move、保留最终 Up”的需求。
- 关闭必须先停止新的回调，再关闭仍由生产者持有的 contact，最后释放 COM，避免 record 生命周期越界。

Reference: https://learn.microsoft.com/en-us/windows/win32/tablet/threading-considerations-for-the-stylusinput-apis

## concurrentqueue constraints

- concurrentqueue 1.0.4 的 `BlockingConcurrentQueue` 由轻量信号量支持；默认 traits 允许信号量先自旋。
- 派生 traits 并令 `MAX_SEMA_SPINS=0`，使完全空闲的绘制线程直接进入内核等待。
- 队列只需要序列化不可丢失的 Down 和控制唤醒。Move/Up 由稳定 record 的原子快照传递，不形成队列积压。
- Down 使用 `try_enqueue` 优先复用预分配容量，失败再 `enqueue`；控制唤醒使用 pending 位合并，避免窗口事件洪泛。

Reference: https://github.com/cameron314/concurrentqueue/blob/v1.0.4/README.md

## Key risk conclusions

- 多字段结构不能假定整体原子；标量原子加奇偶 sequence 才能提供跨字段一致性。
- 多生产者可能同时命中同一 contact；Move 可因门闩争用丢弃，但 Up 必须先关闭路由并等写者退出，保证终态覆盖且不可逆。
- “L2 只包含完整笔画”要求 Up 帧重建剩余活动 contact 的共享临时层，不能直接把共享 L1/L0 全量提交到 L2。
- 同帧完成笔画可一次提交的前提是本阶段所有 contact 都使用同一种普通笔覆盖操作；引入荧光笔/橡皮后必须重新定义跨 contact 排序。

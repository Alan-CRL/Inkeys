# RTS 多接触绘制与低占用绘制线程

## Goal

以 Windows RealTimeStylus（RTS）统一接入 Touch、Pen、MouseLeft、MouseRight 多接触输入；同步插件只发布最新一致快照，绘制线程独立完成建模、速度模拟压感和批量分层提交。在全部接触结束后，绘制线程以零自旋信号量阻塞，避免空闲占用。

## Requirements

### 输入与生命周期

- 输入来源统一为 RTS，设备类型值固定为 Touch=0、Pen=1、MouseLeft=2、MouseRight=3；空闲时按 1/2/3 选择普通笔、荧光笔或橡皮，首个 Down 将工具锁定到本批全部 contact。
- Down 必须保存不可变初始点并可靠入队；Move 只覆盖最新快照，允许丢弃中间点；Up 必须发布最终位置、时间和不可逆终态，不得丢失。
- Up 后生产者不再访问记录，记录和模型的回收完全由绘制线程负责；generation 必须阻止槽位复用导致 ABA。
- 快照字段的跨原子一致性必须可验证；`IdtAtomic<T>` 仅允许平凡可复制且始终无锁的类型，`ContactRecord` 禁止复制和移动。
- 首批预分配 32 个稳定地址 contact slot，耗尽时按 32 个一块扩展；Move/Up 热路径不得使用 `unordered_map`、分配、COM 查询或逐包日志。
- RTS 或多点接口初始化失败时明确报错退出，不增加兼容输入回退。

### 队列与唤醒

- 使用已固定的 concurrentqueue 1.0.4 `BlockingConcurrentQueue`，自定义 traits 将 `MAX_SEMA_SPINS` 设为 0，并预分配 256 项。
- 队列只承载不可丢失的 Down handle 和合并的控制唤醒；Move/Up 不入队。
- resize、clear、full-present、DWM 变化和 exit 发布原子请求后必须唤醒空闲绘制线程。
- 有活动 contact 时按当前目标帧率持续绘制；全部 contact 结束且 L0/L1 清空后，二次排空队列再进入零自旋内核等待。
- `timeBeginPeriod(1)` 仅在首个活动 contact 出现后启用，完全空闲后配对 `timeEndPeriod(1)`。

### 绘制与提交

- 每个 contact 使用独立的 ink-stroke-modeler 状态；Down 消费时先输入不可变初始 `kDown`，随后连接当前最新 Move/Up。
- Up 快照必须作为包含最终位置和 QPC 的 `kUp` 输入；Down 后立即 Up 仍需生成点击或短段。
- 速度由绘制线程根据相邻已消费真实快照的位置/QPC 计算；无新快照的动画帧不得重复更新速度。
- 保留现有预测与速度模拟压感，真实硬件压感暂不参与绘制。新增 `retainPredictionOnUp` 开关并默认关闭：关闭时用模型的 `kUp` 结果平滑完成真实尾部且不提交 prediction；开启时把抬笔前最后可见 L0（含 prediction）直接烘干到 L1。
- L2 始终只包含完整笔画。多个 contact 同帧 Up 时只进行一次 L2 resolve、一次 backbuffer 合成和一次 Present。
- 一个 contact 结束时，其余活动 contact 必须保持可见，且不得提前写入 L2；提交后从 CPU 状态重建剩余 contact 的共享 L1/L0。
- resize 保留 L2 并重建活动 L1/L0；clear 延续现有语义，等待全部 contact 结束后执行。
- Release 构建不得输出逐帧日志；Debug 日志需要限频。

### Compatibility and scope

- 保持 Windows 7 SP1 + KB2670838 兼容目标。
- 保持现有 vcpkg manifest、triplet、install 路径和 concurrentqueue 1.0.4 版本不变。
- 保持源码原有 UTF-8 BOM、CRLF 和必要的中文关键注释，不修改未跟踪的 `Vcpkg/`。
- 手掌识别、真实硬件压感和 RTS 失败兼容模式不在本任务范围。

## Acceptance Criteria

- [x] `Release|ARM64` 使用 ARM64 MSBuild 对完整解决方案 Rebuild 成功，C++ modules、两个 Shader 和资源嵌入链均通过。
- [ ] RTS 鼠标、单指及多指可以同时使用空闲时选定的 1/2/3 工具绘制。
- [ ] Down 后立即 Up、快速点击、最后位置变化后立即 Up 均保留最终点和 Up。
- [ ] `retainPredictionOnUp=false` 时抬笔由模型平滑完成真实笔锋且不保留 prediction；设为 `true` 时最后可见 L0 原样进入 L1。
- [ ] 高频 Move 只覆盖快照、不会形成事件积压，Down/Up 不丢失。
- [ ] 多个 contact 同帧 Up 只触发一次 L2 resolve 和一次 Present。
- [ ] 某一 contact Up 时，其余活动笔画不消失、不重复，也不会提前进入 L2。
- [ ] resize 后活动多指可继续；全部结束后 L1/L0 为空。
- [ ] 全部结束后 frame/Present 计数停止，绘制线程阻塞在信号量；新 Down、resize、clear、DWM、full-present 和 exit 可立即唤醒。
- [ ] 活动 contact 静止时仍按目标帧率运行。
- [ ] 未跟踪 `Vcpkg/` 未被改动，vcpkg 配置与依赖版本未被改动。

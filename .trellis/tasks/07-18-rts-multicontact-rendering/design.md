# RTS 多接触绘制设计

## Boundaries

新增两个原生 C++ 模块：

- `draw3.contact_input`：公开输入值类型、稳定地址 contact 记录、对象池、无锁路由、Down/控制队列及回收接口。
- `draw3.realtime_stylus`：封装 COM/RTS 初始化、同步插件、packet 元数据缓存和关闭顺序。

`DrawingController` 成为唯一消费方并继续独占 D3D/D2D 与 ink-stroke-modeler。`WindowController` 只发布控制原子请求并请求一次合并唤醒。旧 Win32 mouse poll 接口保留但不再作为主绘制入口。

## Public contracts

`draw3.contact_input` 提供：

- `InputDeviceType { Touch=0, Pen=1, MouseLeft=2, MouseRight=3 }`
- `ContactPhase { Down, Move, Up, Cancelled }`
- `PointF`、`SizeF`、`ContactSnapshot`
- `ContactRecord`：不可变 identity/down sample，加原子 x/y、pressure、w/h、QPC、phase、sequence 与生产状态；删除复制和移动。
- `ContactHandle { ContactRecord*, generation }`
- `ContactInputCoordinator`：Down/Move/Up/Cancel、队列等待/排空、控制唤醒、消费方回收与关闭。

`IdtAtomic<T>` 保留平凡复制和 `std::atomic<T>::is_always_lock_free` 编译期约束。为避免 ARM64/x64/x86 对复合原子能力不同，快照使用独立的标量原子，不把整个 `PointF` 或 `ContactSnapshot` 作为一个原子。

## Contact state and coherent snapshots

每个固定地址 slot 具有 `Free -> Initializing -> Producing -> Closing -> ConsumerOwned -> Free` 状态和递增 generation。

Down 在慢路径取得空闲 slot，填充 immutable identity/down sample 和 decoder 缓存，发布初始偶数 sequence 后切换为 `Producing`，再把 handle 可靠入队。先尝试无分配入队，失败再走普通 `enqueue`；只有内存耗尽可失败。

Move 扫描已发布的 32-slot 块定位 `{tcid,cid}`，尝试取得单写门闩。争用失败即可丢弃。取得后再次确认状态/generation，通过奇偶 sequence 发布 x/y/pressure/size/QPC/phase；初版全部采用 `seq_cst`。

Up 先以 CAS 将路由从 `Producing` 关闭到 `Closing`，等待当前写者退出，再以相同 sequence 协议发布最终点与 `Up`，最后切换为 `ConsumerOwned`。此后 RTS 不再触碰记录。Cancelled 使用相同不可逆关闭路径。

消费方读取时先读偶数 sequence、读取所有标量、再复读 sequence；两次相同且为偶数才得到一致快照。handle 的 generation 不匹配时视为失效。

块链表只增加不删除，块内地址在协调器销毁前稳定。首块预分配 32 个 slot，耗尽时在罕见慢路径锁内扩展一块；Move/Up 扫描已发布块，不使用热路径哈希表。

## Queue and wake protocol

协调器内部使用 `BlockingConcurrentQueue<IngressCommand, LowPowerTraits>`：

- `LowPowerTraits::MAX_SEMA_SPINS = 0`
- 构造时预分配 256 项
- `IngressCommand` 仅包含 `Down(ContactHandle)` 与 `ControlWake`

控制唤醒由一个原子 pending 标志合并：第一个发布者入队 `ControlWake`，后续请求只更新对应的窗口原子标志；消费到唤醒后清 pending，并重新检查所有请求。Down 不合并且不得丢。

活动 contact 存在时绘制线程不等待队列，逐帧 try-dequeue 并读取所有最新快照。完全没有活动 contact 且 L1/L0 已恢复单位操作后，先排空一次、复查窗口状态和活动数，再调用 `wait_dequeue`。该二次检查避免入睡窗口丢唤醒。

## RTS adapter

`RealTimeStylusInput` 在主线程以 `COINIT_MULTITHREADED` 初始化 COM，创建并绑定 `IRealTimeStylus`，要求 `IRealTimeStylus3::MultiTouchEnabled=TRUE`，注册轻量同步插件并启用 RTS。

仅请求 X/Y packet。pressure、w/h 保留为未知值，接口字段先稳定下来。插件在 Enabled/TabletAdded 或 Down 慢路径缓存各 `tcid` 的 X/Y 索引、缩放和设备种类；Packets 热路径只解析批次最后一个完整 packet，不执行 COM 查询、分配或逐包日志。Down/Up 按单个完整 packet 解析并在回调处读取 QPC。

Touch/Pen 使用缓存 `TabletDeviceKind` 区分；鼠标通过实时按键状态区分 MouseLeft/MouseRight。同步插件只解析并调用协调器发布，模型、速度、D3D 和日志均不进入回调。

关闭顺序固定为：禁用 RTS、移除插件、把所有仍由生产者持有的 contact 发布为 Cancelled、Release COM 对象、`CoUninitialize`。初始化任一步失败均返回明确错误，由主程序退出；本任务不启用旧鼠标回退。

## Drawing runtime

`DrawingController::Run()` 取代单笔 `DrawMouseStroke()` 主循环。绘制线程预热 16 个可复用 `ActiveStroke`，每个对象有独立 modeler、CPU 稳定几何、上一帧可见尾部/预测和最后已消费输入。容器预留容量，Reset 时清长度但保留分配；超过 16 时仅由绘制线程扩展。

消费 Down 时先把 immutable down sample 作为 `kDown` 输入该模型，再读取当前一致快照。如果期间已经 Move/Up，则直接连接初始点与当前点。只有 sequence 变化时才消费真实快照、计算两点距离/QPC 差及模拟速度；动画帧仍可更新模型输出，但不伪造速度样本。Up 直接作为最终 `kUp` 输入。

## Layer batching

所有 contact 本阶段使用相同普通笔覆盖运算，因此可以共享临时层：

1. 排空 Down，读取所有活动 contact 的一致快照并更新独立模型。
2. 无 Up 时，各 contact 的稳定增量继续累积至共享 L1；每帧只清一次 L0，再依次绘制全部实时尾部与预测。
3. 有 Up 时，清空 scratch，把本帧所有已完成 contact 的稳定前缀、上一帧可见尾部和最终 Up 几何合并画入 L1，只调用一次 `ApplyOperatorLayers` 写入 L2。
4. 清空 L1，从剩余活动 contact 的 CPU 稳定几何重建共享 L1，再清空并重建共享 L0。
5. 合并旧/新 L0、稳定增量、完成笔画与窗口请求的 dirty rect，只合成一次 backbuffer 并 Present 一次。
6. L2 提交与重建成功后，才移除结束 contact、Reset 模型并把 slot 归还协调器。

严格禁止把仍活动 contact 的任何几何提交到 L2。以后增加荧光笔或橡皮时，不同运算的跨 contact 顺序会破坏这一合并前提，必须重新设计。

resize 保留 L2，重建目标后从所有活动 contact 的 CPU 状态恢复 L1/L0。clear 在仍有活动 contact 时继续延后，避免清屏后笔画重新出现。

## Timing, logging, and compatibility

首个活动 contact 出现时调用 `timeBeginPeriod(1)`，最后一个完成且临时层清空后调用配对的 `timeEndPeriod(1)`。活动期间沿用 `target_fps` 和现有高精度帧等待；完全空闲时只在队列信号量上等待。

Release 关闭逐帧日志，Debug 仅保留限频诊断。新代码不得依赖 `atomic::wait`、`WaitOnAddress` 等破坏 Windows 7 目标的 API；阻塞由 concurrentqueue 1.0.4 的兼容信号量实现。

## Failure and rollback

RTS 初始化失败在进入绘制循环前完成清理并返回非零；运行期的 slot 扩展或队列分配失败不得产生半发布记录。若编译或运行验证失败，可以保留新模块而将 `main.cpp` 恢复到旧入口作为局部回滚，但最终交付必须使用 RTS 新入口，不能以回退模式掩盖错误。

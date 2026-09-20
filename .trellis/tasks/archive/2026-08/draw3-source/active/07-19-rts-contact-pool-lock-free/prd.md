# RTS contact 对象池无锁优化

## Goal

消除 RTS Down 正常路径中的对象池互斥锁、线性逐槽扫描和队列大消息，使已预分配容量内的 contact 获取、Down 入队、Move/Up 路由及回收均不依赖应用级锁或运行时分配，同时保持当前多生产者、单绘制消费者的生命周期和终态可靠性。

## Background

- 当前 `ContactBlock` 每块包含 32 个稳定地址 `ContactRecord`，协调器构造时预分配首块。
- `AcquireFreeSlot()` 每次 Down 都取得 `allocationMutex`，随后逐块逐槽执行 `Free -> Initializing` CAS；全部占用时还会在 RTS 回调路径分配新块。
- Down 队列当前传递 `IngressCommand { kind, ContactHandle { pointer, generation } }`。concurrentqueue 不要求 payload 自身成为整体原子，但本任务将消息收紧为单个 8 字节指针。
- 只有绘制线程在 L2 提交完成后回收 slot，因此 Down 命令出队前 slot 不会被复用。
- generation 仍然是生产者路由的必要保护：旧 Move/Up 可能跨越关闭、回收和同地址复用，必须通过 generation 与 route state 拒绝。

## Requirements

### R1. 无锁预分配对象池

- 每个 32-slot block 使用始终无锁的 32 位原子空闲位图管理 slot。
- 已有空闲 slot 时，Down 获取只能使用原子 load/CAS，不得取得 `std::mutex`、等待其他线程或调用分配器。
- 每个 slot 必须保存初始化后不变的 owner block/slot index 信息，以支持 O(1) 回收。
- 回收必须先完成 `ConsumerOwned -> Free` 生命周期转换，再 release 发布空闲位，避免尚未清理的 slot 被重新取得。

### R2. 8 字节 Down 队列消息

- ingress queue 的值类型改为 `ContactRecord*`；非空指针表示 Down，空指针表示合并后的 `ControlWake`。
- 队列不得对 16 字节 `ContactHandle` 或整个 `ContactRecord` 建立 `std::atomic<T>`。
- 绘制线程出队后读取 record 当前 generation，并在自己的 `RuntimeStroke`/handle 中保存；该本地 16 字节状态不属于跨线程原子接口。
- `controlWakePending` 的 sticky 原子协议和阻塞消费者唤醒语义保持不变。
- 初始化阶段创建 32 个可原子独占的 Down producer token 和一个控制 producer token；热路径只调用 token 版 `try_enqueue`。
- queue 使用三参数构造器按显式 producer 数预留 blocks，并禁用 implicit producer；不得回退到可能注册 producer 或分配 block 的无 token API。

### R3. generation 与终态安全

- generation 继续编码在 `producerRoute_`，并与 producer state 一起进行精确 CAS。
- Move/Up 在取得候选 record 后必须再次验证 generation、route state、`tcid` 和 `cid`，防止旧生产者写入复用后的 contact。
- Up/Cancelled 继续先关闭 route、等待已进入的 Move writer，再发布不可逆终态。
- generation 不得因为队列改为裸指针而从生产者路由中删除。

### R4. 容量与扩展

- 支持容量在启用 RTS 前按 `round_up_32(max(32, 2 × (SM_MAXIMUMTOUCHES + 2)))` 完成预分配，其中 `+2` 为 Pen/Mouse 余量，`×2` 覆盖活动与 closing/consumer 交接。
- queue 容量为 `max(256, slotCapacity + 1)`。
- RTS 同步回调不得在容量耗尽时取得扩容 mutex 或调用堆分配器。
- 容量耗尽必须返回明确失败并保持 route/slot 可恢复，不得覆盖活动 contact、复用未回收 slot 或静默进入带锁慢路径。
- 若未来需要运行时扩容，应由独立任务设计非 RTS 线程补充机制，不在本任务中临时加入回调内扩容。

### R5. 扫描与兼容性

- `FindProducing()` 应利用 occupied bitmap 只检查已占用 slot；块链仍保持只追加、稳定地址和 acquire/release 发布。
- 保持 Windows 7 SP1 + KB2670838、x86/x64 与当前 ARM64 目标的源码兼容；只使用项目已接受的 C++/Win32 原语。
- 不改变 RTS 坐标、工具选择、L0/L1/L2、预测、Present、clear/resize 或 concurrentqueue 版本和 vcpkg 路径。

## Acceptance Criteria

- [x] `PublishDown()` 在预分配容量内不执行 mutex lock、堆分配或逐个 32-slot record CAS 扫描。
- [x] ingress queue 的 value type 是 `ContactRecord*`，并有编译期检查确认其大小等于 `uintptr_t`；空指针只表示 `ControlWake`。
- [x] 每块使用 32 位 lock-free 原子位图；并发生产者不能取得同一 slot，回收后 slot 可以再次取得。
- [x] stale generation 的 Move/Up、重复回收和旧 consumer handle 均不能修改或释放新 generation contact。
- [x] Down 后立即 Up、Move 与 Up 并发、Cancelled/shutdown、32-slot 边界及容量耗尽均保持确定性状态。
- [x] 完全空闲时仍由 `BlockingConcurrentQueue` 零自旋阻塞；新 Down 和 ControlWake 能立即唤醒。
- [x] 32 个 Down token 可并发独占，控制 token 独立；预留容量内 token 版 `try_enqueue` 不触发隐式 producer 或分配回退。
- [x] ARM64 Release 全解决方案 Rebuild 成功，两个 Shader、C++ Modules、资源和最终链接均通过。
- [x] `git diff --check` 通过，源码保持 UTF-8 BOM + CRLF，Trellis 文档保持 UTF-8 无 BOM + LF；构建生成的未跟踪 `Vcpkg/` 缓存不进入提交。

## Out of Scope

- 不调整墨迹几何、压感、荧光笔 cap、预测或呈现行为。
- 不替换 concurrentqueue 1.0.4。
- 不实现无界、运行时动态增长且完全不依赖分配器的对象池。
- 不为 Move 建立逐点队列；Move 仍覆盖最新原子快照。

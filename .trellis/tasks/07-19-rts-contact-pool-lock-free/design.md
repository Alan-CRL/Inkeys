# RTS contact 对象池无锁优化设计

## 1. 边界与目标

优化范围限定在 `draw3.contact_input` 的 contact slot 获取、Down ingress 消息、生产者查找和绘制线程回收。RTS packet 解码、stroke modeler 和渲染分层保持不变。

目标不是让 `ContactRecord` 或 16 字节 handle 成为整体原子，而是通过“独占 slot 普通写入 + 小型原子发布”维持无数据竞争的所有权协议。

## 2. Block 与空闲位图

每块保持 32 个稳定地址 record，并增加一个始终无锁的 32 位位图：

```cpp
struct ContactBlock
{
    std::array<ContactRecord, 32> records;
    IdtAtomic<uint32_t> freeMask{0xFFFFFFFFu};
    std::atomic<ContactBlock*> next = nullptr;
};
```

- bit=1：slot 可取得。
- bit=0：slot 正在初始化、生产、关闭或归消费者所有。
- 获取方从一次 mask snapshot 中选择最低有效位，通过 CAS 清零；失败后使用 CAS 返回的新 mask 重试。
- record 在 block 创建阶段写入不可变的 owner/slot index。回收方不扫描 block，也不依赖外部哈希表。
- `producerRoute_` 继续作为 contact 生命周期真值；bitmap 只负责“是否允许新的 Down 取得该地址”。

## 3. 获取与回收顺序

### Acquire

1. acquire/load block 的 `freeMask`。
2. CAS 清除一个空闲 bit，获得该 slot 的独占初始化权。
3. 读取旧 generation，生成下一代并初始化 immutable Down 与原子 snapshot。
4. route 从 `Initializing` 发布为 `Producing`。
5. 将非空 `ContactRecord*` 入队。
6. 入队失败时关闭 route、等待 writer、恢复 `Free`，最后重新置位 bitmap。

### Recycle

1. 校验 consumer 保存的 expected generation。
2. 精确 CAS `ConsumerOwned -> Free`。
3. 清理完成后对 owner block 的 free bit 执行 release 发布。
4. 重复或过期回收不设置 bitmap，避免释放新 generation。

bitmap 位不得先于 route/内容清理发布，否则新的 Down 可能观察到半清理 slot。

## 4. 8 字节 ingress 协议

```cpp
using IngressQueue =
    moodycamel::BlockingConcurrentQueue<ContactRecord*, LowPowerQueueTraits>;
```

- `record != nullptr`：可靠 Down。
- `record == nullptr`：合并的 `ControlWake`。
- resize、clear、full-present、DWM 和 exit 的具体状态仍由现有 sticky atomic 保存，因此只需要一种控制唤醒值。
- 绘制线程出队后验证 record state，读取 generation，并构造线程私有 handle；slot 在该消费者完成 L2 与 `Recycle()` 前保持 pin，不会复用。

消息缩小不改变 concurrentqueue 的算法性质；本设计的直接收益是接口最小化、避免对 16 字节整体原子的误依赖，并减少队列缓存占用。

### 显式 producer token

coordinator 构造期创建 32 个 Down `producer_token_t` 和一个控制 token。32 位空闲 token mask 通过 CAS 清位/置位提供临时独占；Down 回调在入队前借用 token，完成 `try_enqueue(token, record)` 后立即归还。控制唤醒始终使用其专用 token。

queue 使用三参数构造器：

```text
BlockingConcurrentQueue(queueCapacity, 33 explicit producers, 0 implicit producers)
```

预留 producer block 后，热路径不调用无 token `try_enqueue` 或 `enqueue`，以免隐式注册 producer 或在回调中分配。

## 5. 生产者查找

`FindProducing()` 遍历 acquire 发布的 block 链，对每块读取 `occupied = ~freeMask`，再通过 bit scan 只访问 occupied slot。候选仍必须验证：

```text
route == (capturedGeneration, Producing)
tabletContextId == tcid
contactId == cid
```

找到候选后，Move 获取 writer latch 并再次验证 exact route；Up 先 CAS route 到 Closing，再等待 writer。bitmap 不能替代这些 generation/state 检查。

## 6. 容量策略

- 在 RTS 启用前计算并建立全部 block。
- slot 容量为 `round_up_32(max(32, 2 × (SM_MAXIMUMTOUCHES + 2)))`。
- queue 容量为 `max(256, slotCapacity + 1)`，并按 33 个显式 producer 预留 blocks。
- 回调期间不修改 block 所有权容器，不申请 block，不取得 expansion mutex。
- 耗尽是明确的容量错误路径；必须保证已存在 contact 和队列状态不受影响。

这样牺牲无界动态增长，换取 RTS 同步回调可证明的无锁、无分配上界。未来若必须无损支持超过声明容量的设备，需要单独设计后台预补充，而不能把堆分配重新放回回调。

## 7. 内存序与兼容性

- 初版沿用项目当前 `seq_cst` 的 `IdtAtomic` 默认值，先保证状态证明清晰。
- block 链发布继续使用 release store / acquire load。
- bitmap 回收至少使用 release，获取至少使用 acquire/acq_rel。
- 只对 `uint32_t`、`uint64_t`、指针和现有标量使用原子；不得新增 16 字节 `std::atomic<T>`。
- `static_assert(std::atomic<uint32_t>::is_always_lock_free)` 与指针大小检查在 ARM64/x64/x86 编译期生效。

## 8. 风险与回滚

- 最大风险是 bitmap 与 route 发布顺序不一致，导致重复分配或永久泄漏 bit。
- 第二风险是空指针 ControlWake 与异常 Down 混淆；`PublishDown` 永远不得入队空 record。
- 回滚时可以恢复旧 `IngressCommand` 和扫描池，但不得通过保留 bitmap 同时继续使用旧 mutex/逐槽状态争用形成双重所有权真值。

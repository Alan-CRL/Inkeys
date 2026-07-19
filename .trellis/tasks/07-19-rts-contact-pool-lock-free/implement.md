# RTS contact 对象池无锁优化实施计划

## 1. 对象池元数据

- [x] 为 `ContactBlock` 增加 32 位 lock-free `freeMask`。
- [x] 为每个 `ContactRecord` 初始化不可变 owner block、slot index/bit；保持 record 不可复制、不可移动和稳定地址。
- [x] 增加位图选择、CAS 取得、精确归还 helper，并为关键发布顺序添加简短中文注释。

## 2. Down 获取与容量

- [x] 用 block bitmap CAS 替换 `AcquireFreeSlot()` 的每次 mutex + 逐槽扫描。
- [x] 在 RTS 启用前按 `round_up_32(max(32, 2 × (SM_MAXIMUMTOUCHES + 2)))` 预分配全部 block；queue 容量为 `max(256, slotCapacity + 1)`。
- [x] 定义容量耗尽诊断与失败回滚，保证未入队 Down 不遗留 occupied bit 或 producing route。
- [x] 保持 generation 递增、DownSnapshot 不可变发布和 `Producing` 状态顺序。

## 3. 8 字节 ingress queue

- [x] 将 queue value type 改为 `ContactRecord*`，非空为 Down、空指针为 `ControlWake`。
- [x] 构造期创建 32 个 Down producer token 与一个控制 token，用原子位图保证每个 Down token 同时只被一个回调使用。
- [x] 用三参数 queue 构造器预留显式 producer blocks、禁用 implicit producer；热路径只调用 token 版 `try_enqueue`。
- [x] 修改可靠 Down 入队、控制唤醒、TryDequeue/WaitDequeue 和 DrawingController 命令消费接口。
- [x] 绘制线程出队后捕获 generation，构造线程私有 handle；L2 提交完成前继续 pin slot。
- [x] 添加编译期指针大小/lock-free 标量检查，不新增 16 字节整体原子。

## 4. Move、Up 与回收

- [x] `FindProducing()` 使用 occupied bitmap 跳过空闲 slot，候选仍执行 generation/state/tcid/cid 校验。
- [x] 保留 Move try-latch 丢弃策略和 Up/Cancelled 的 Closing + writer drain + ConsumerOwned 交接。
- [x] Recycle 精确校验 generation，先完成 route Free，再 release 归还 bitmap；过期或重复回收不得置位。
- [x] 检查 shutdown/Disabled/Error 批量 Cancelled 与位图状态不会死循环或泄漏。

## 5. 并发与回归验证

- [x] 增加或编写可重复的并发压力验证：多个生产者同时 Down 时 slot 唯一，无重复 bit、无活动 slot 被覆盖。
- [x] 验证 Move 与 Up 竞争、旧 generation 延迟 Move、Down 后立即 Up、重复回收及容量边界。
- [x] 验证32-slot 全占用、释放再取得、多个 block 扫描和容量耗尽回滚。
- [x] 验证空闲 `wait_dequeue` 零自旋、新 Down/ControlWake 唤醒以及控制请求不丢失。
- [ ] 人工验证 Mouse/Pen/Touch 多指绘制行为与修改前一致，不把荧光笔 cap 已知问题混入本任务。

## 6. 质量门

- [ ] 使用 ARM64 MSBuild 对完整解决方案执行 Release Rebuild，超时不少于10分钟：

  ```powershell
  & 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' `
    '.\inkStrokeModelerTest.sln' /t:Rebuild /m:1 /nr:false `
    /p:CL_MPCount=1 /p:UseMultiToolTask=false `
    /p:Configuration=Release /p:Platform=ARM64 /verbosity:minimal
  ```

- [x] 确认 C++ Modules、两个 Shader、资源和最终链接成功。
- [x] 执行 `git diff --check`，检查 C++ UTF-8 BOM + CRLF、Trellis 文档 UTF-8 无 BOM + LF。
- [x] 确认 concurrentqueue 版本、vcpkg manifest/triplet/install 路径未修改；构建生成的未跟踪 `Vcpkg/` 缓存继续排除在提交外。
- [x] 运行 `trellis-check` 做并发所有权、ABA、队列唤醒、生命周期和构建复核。

## Rollback Points

- 位图对象池与指针队列分别提交/验证，便于定位所有权错误。
- 若位图实现出现重复分配，立即回退整个 bitmap 所有权协议，不保留 bitmap 与旧逐槽获取的混合路径。
- 若空指针 ControlWake 影响唤醒，先恢复显式命令类型，不修改窗口 sticky atomic 请求协议。

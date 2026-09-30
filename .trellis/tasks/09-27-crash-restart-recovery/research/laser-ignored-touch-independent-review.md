# F-060 Laser 额外 Touch route 寿命独立复审

审查截止：2026-09-28 23:47 UTC。`Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` 当前 Git blob 为 `6b77b771c45479a5a35a65c07280853fcd8c752a`，与实施者冻结指纹一致；本轮在共享未提交工作树上只读核源码、H0 原分支、F-057 状态机、F-060 设计和实施原始日志，只写本报告。`git diff --check -- Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` exit 0。H0→当前 Controller 含多个子任务改动（`git diff --numstat` 为 3592/738 行），因此此结论仅覆盖下述 F-060 局部代码与其直接依赖，不把整文件 diff 判为通过。

## Findings (fixed in candidate)

- **已确认，原 P1 slot 泄漏已修：** `Draw3.DrawingController.cpp:5338-5359` 的 `initializeStroke` 只在活动 runtime 仍物理存活、其工具为 Laser 且设备为 Touch 时标记已有首指；当前生产入口调用同 TU 的 `IgnoreAdditionalLaserTouch`（`:1839-1848`）。该 helper 在 `batchTool==Laser`、新设备 `Touch`、已有 Laser Touch、且多指开关关闭四项条件同时成立时，对精确 `ContactHandle` 调用 `DiscardUntilTerminal` 并返回拒收。H0 对相同四项条件直接 `Recycle(handle)` 后丢失 handle（H0 Controller 原 `:2547-2553`）；Down 路由仍为 Producing，`Recycle` 不释放它。当前 `ContactInput.cpp:858-880` 使 Producing→Quarantined，`Close:564-640` 在物理 Up/Cancel 后 Free 并 ReleaseSlot。由此补上被忽略第二指的 owner，首指及不满足条件的路径保持正常入口。
- **已确认，Closing 分支有界：** 若 Up/Cancel 已抢先把第二指变为 Closing，`DiscardUntilTerminal:871-880` 用同代 CAS 改 ClosingDiscarded 或在 producer 完成后回收 ConsumerOwned；它不等待 producer 写终态。`ContactInput.cpp:617-635` 的唯一 Close producer 对 ClosingDiscarded→Free 负责释放；旧 generation、重复 Discard 不会再次释放。若已 ConsumerOwned，`Recycle:837-855` 负责释放。此处依赖 F-057 状态机冻结版，而非 F-060 另加回收特例。
- **已确认，生产与测试使用同一完整判定：** `RunIgnoredLaserTouchProductionTest:3166-3335` 和 `initializeStroke:5356-5359` 调同一个 helper；测试不复制 route 算法。首指尚不活跃、开关开启、非 Touch、非 Laser 均走 false；32 次第二 Touch 交替真实 `PublishUp/PublishCancelled`，检查第二槽自动释放、复用时 generation 前进和首指持续可读；Closing hook 在真实 Close CAS 后暂停 producer，检查 helper 可先返回。测试的 `Recycle(ignored)`（`:3254`、`:3325`）是在断言后清理红版残留，不参与绿版提前回收的判定。测试通过 `IdtMain.cpp:697-698` 的无窗口 CLI 入口运行。
- **其它 Controller Recycle 调用未见同类确定泄漏：** `:5007` 的 pan handoff 有 Up/Cancelled 条件，`:5620` 的 reconnect 旧 handle 已由终态交出，`:5714-5716`、`:5812-5814`、`:5872-5874` 的失败回退先同步 `PublishCancelled`，`:6622-6627` 的 gesture 回收仅接受 Up/Cancelled；测试中的其余调用也在终态后。若 Cancel 与真实 Up 竞争，当前 `Recycle` 还处理 Closing→ClosingDiscarded。没有发现另一个已出队 Producing Down 被直接 Recycle 后丢弃的路径。F-060 helper 只在 Down 初始化执行，无分配或新锁；相较 H0，开关的 acquire load 现在因函数实参求值对每次 Down 都执行一次，而 H0 的短路条件仅在 Laser/Touch/已有首指时读取，属小额热路径成本，当前无性能阻断证据。

## Findings (not fixed / verification boundary)

- **本轮无新确认产品阻断项。** CLI probe 调用的是与生产共用的 helper 和真实 `ContactInputCoordinator`，但不运行完整 `initializeStroke` 的 batch 选择、Laser 粒子渲染、真实 RTS 多触点或 Host GUI；`hasActiveLaserTouchContact` 在 probe 由参数模拟，生产计算由本轮静态核对。它证明局部 owner 合同，不证明用户截图画布卡死的根因已经定位。
- 隐藏 Host 的受限沙箱运行在 PPT `io_error` 后有 Selection/end-page held-contact 断言失败；实施者在可访问隔离数据 root 的沙箱外，**最终冻结版**再次运行同一隐藏入口，日志 `f060-laser-final-debug-arm64-hidden-unsandboxed.stderr.log:17,34-35` 含真实 Host held-contact 和 all hidden integration PASS。两种环境结论必须分列；本轮没有亲自复跑。Win7、Release 各架构、真实触摸硬件/激光视觉与用户现场线程栈未验。
- F-060 实施者保留的红→绿证据为 `TestResults/release-hardening/f060-laser-red-debug-arm64.stderr.log:4` 唯一 Laser 自动退休失败、`f060-laser-final-debug-arm64.stderr.log:4` Laser PASS；最终 Debug|ARM64 Solution Build 日志 `f060-laser-final-debug-arm64-build.log:349,366-367` 为 Build succeeded、10 warnings、0 errors。Headless exit 0 与最终隐藏 Host exit 0 由实施记录及日志提供；这是实施者执行的结果，非独立复跑。红版 probe 会在断言后手动 Recycle 使后续迭代不因泄漏污染。

## Verification

- 静态：当前 blob 与冻结指纹匹配；核 H0 原四项判定和旧 Recycle、当前 helper/唯一生产调用、真实 ContactInput 状态机、同文件所有 `input_.Recycle` 调用点、CLI 登记、原始红绿/隐藏日志；`git diff --check` pass。
- Lint/TypeCheck/Build/Tests：本轮未运行；父任务明确要求独立只读且另有 worker 占用构建槽。现有 Debug|ARM64、Headless、隐藏 Host 结果只作实施者证据。

独立结论：F-060 的局部修复满足“拒收第二 Laser Touch 后由物理终态回收 slot”的安全合同，可解除该已确认泄漏的代码阻断；整体画布卡死/跨模块发布门仍须由父任务按各自证据判断。

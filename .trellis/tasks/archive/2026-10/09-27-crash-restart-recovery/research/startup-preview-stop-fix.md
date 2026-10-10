# F-010 StartupPreview owner 停止竞态：实施与静态复核

## 修改边界

- 生产改动仅在 `Inkeys/Inkeys/UI/StartupPreview/StartupPreview.cpp` 的 `PreviewOwner`/`OwnerState`；不改变窗口样式、预览视觉、启动超时、RenderPipeline、Draw3 或主进程重启路径。
- 最小行为差：Stop 或渲染回调请求停止后，owner 即使尚未建立消息队列或 HWND，迟到完成创建时也能看见同锁保护的 `stopRequested` 并自行退出。

## 状态与时序

`RequestStop` 和 `Stop` 现在共用一个发布入口：先在 `OwnerState::mutex` 下置位 sticky `stopRequested`，再设置同一 `OwnerState` 持有的手动复位事件；向 HWND 发送 `OwnerStopMessage` 或向线程发送 `WM_QUIT` 仅作为备用唤醒。任一消息投递失败不撤销请求。线程已 `exited` 时不向可能重用的线程 ID 再投递。`Stop` 仍最多等待 1500 ms，超时只 detach 持有 `shared_ptr<OwnerState>` 的 owner 线程。

事件通过 `CreateEventW(nullptr, TRUE, FALSE, nullptr)` 在派生 owner 线程之前建立，无继承、无全局名称；若创建失败，Preview `Start` 直接失败并由原启动路径降级，不创建 HWND。最后一个 `OwnerState` 引用销毁时关闭 handle，包括有界 Stop 后线程迟到退出的情况。

Owner 线程读取停止请求的三处边界：

1. `CreateWindowExW` 前：超时请求已到则跳过创建。
2. 创建/SetWindowPos 后发布 `window/ready/creationSucceeded` 时：若请求已到，不把 HWND 发布为可用，也不返回成功。
3. 进入消息等待前：补上“发布后、等待前”窗口；发现请求则在 owner 线程 Hide/Destroy。检查之后才来的请求以 event 唤醒 `MsgWaitForMultipleObjectsEx`，即使 `PostMessageW` 失败也能退出。

消息循环同时等待 event 和队列，使用 `MWMO_INPUTAVAILABLE` 保证已在队列中的消息也会唤醒；每次派发前检查 sticky stop。event 唤醒优先退出，`WM_QUIT` 或等待失败也退出；随后在 owner 线程隐藏并销毁仍有效的 HWND。每 1000 ms 仅检查一次 sticky stop，用于 `SetEvent` 与备用消息投递同时失败的极端情况；普通消息到达仍立即唤醒，不增加可见输入等待。`ready` 与 `exited` 都沿原条件变量通知；锁内未调用 Win32 窗口操作。正常创建、Show、handoff 的路径继续派发原窗口消息。

| 交错 | 静态结果 |
| --- | --- |
| Stop 早于 threadId/消息队列 | sticky 标志留存；owner 创建前检查并结束；不依赖投递成功。 |
| Stop 在 CreateWindowExW 内或 SetWindowPos 后、window 发布前 | 发布阶段看见标志；迟到 HWND 由创建线程销毁，Start 不返回可用 owner。 |
| Stop 在 window 发布后、进入消息等待前 | owner 再查标志；若已经检查，event 为有状态唤醒。 |
| Stop 在消息循环中且 PostMessageW 失败 | event 唤醒并退出；若 SetEvent 也失败，最多一次 1000 ms 等待后检查 sticky 标志。 |
| Stop 与 WM_QUIT 同时到达 | 消息循环退出，并在 owner 线程隐藏/销毁仍有效的窗口。 |
| Stop 在 owner 已退出之后 | `exited` 阻止再投递；等待谓词立即满足。 |

`PreviewOwner` 本身只由启动/公开 Stop 的主调用链及其 Runtime 管理；owner thread 只捕获 `OwnerState`，不持有 Runtime/PreviewOwner，因而此改动没有从 owner thread 调用 `Stop()->join()` 的路径。`Stop` 保留锁外 `join/detach`。

## 已执行与剩余验证

- `git diff --check -- Inkeys/Inkeys/UI/StartupPreview/StartupPreview.cpp`：退出码 0；diff 仅目标文件的状态/时序分支。
- 文件仍为无 BOM UTF-8、CRLF；未运行 GUI/HWND/异常注入。
- 官方 API 文档给出 `CreateEventW`、`SetEvent`、`MsgWaitForMultipleObjectsEx` 的最低客户端均早于 Win7；这只证明 API 可用性，不证明 Win7 SP1 + KB2670838 真机运行通过：[CreateEventW](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createeventw)、[SetEvent](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-setevent)、[MsgWaitForMultipleObjectsEx](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-msgwaitformultipleobjectsex)。
- 完整 `InkeysRepo.sln Debug|ARM64` 与严格 `InkeysHeadlessTests --no-window` 由主任务在并行 MSBuild 完毕后统一运行。Headless 现有 Preview reducer 测试不触及 owner HWND 生命周期，不能冒充本竞态动态 PASS。
- 真正的迟到 owner、窗口残留和正常 handoff 仍需要隔离 GUI 进程故障注入；若 Win32 创建调用本身永不返回，sticky 请求无法强制中断系统调用，仍是边界。

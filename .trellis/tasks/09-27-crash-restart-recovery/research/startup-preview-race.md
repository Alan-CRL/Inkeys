# F-010：StartupPreview owner 超时停止竞态（静态调查）

## 结论与证据等级

- **已确认静态可达的生命周期缺口**：启动 owner 线程超过 2 秒仍未发布 `ready` 时，失败清理可能在目标线程尚无消息队列的阶段发送 `WM_QUIT`；返回值未检查，1.5 秒后 `detach`，迟到线程也没有查询持久的停止请求。不能把这条路径视为已可靠停止。尚未在隔离进程里复现迟到线程、残留窗口或用户输入失效。
- **影响边界**：该窗口以 `WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE` 和不带 `WS_VISIBLE` 的 `WS_POPUP` 创建；`SetWindowPos` 没有 `SWP_SHOWWINDOW`。如果 owner 创建在预览注册渲染回调之前超时，遗留窗口通常仍隐藏，已证实的风险是进程内 owner 线程/HWND 生命周期失控和后续停止无法回收。不能仅凭 topmost 样式宣称其已经拦截用户输入。
- **优先级建议**：P2／中，启动失败路径与资源生命周期。默认配置启用预览；触发还需要 owner 创建阶段异常延迟及线程消息投递失败，实际发生率未测。

## 实际编译和调用链

`Inkeys/Inkeys.vcxproj:853–856` 把 StartupPreview 模块加入主程序，`InkeysHeadlessTests/InkeysHeadlessTests.vcxproj:114–115` 也编译同一生产模块。`IdtMain.cpp:945–993` 在共享 RenderPipeline 初始化后由主线程调用 `StartupPreview::Start`；失败时 `:994–1002` 降级继续启动。`StartupPreview.cpp:925–988` 创建局部 `Runtime`，在 `:953` 同步调用 `runtime->owner.Start`；owner 阶段返回 false 时局部 `Runtime` 析构，`PreviewOwner::~PreviewOwner` 在 `:173` 调用 `Stop`。即使 owner 创建成功，渲染首帧等待 `:973–980` 超时也走公开 `Stop`。正式关闭路径 `IdtMain.cpp:2106–2108` 先撤 topmost observer 再公开 `Stop`；`StartupPreview.cpp:990–1004` 先撤活动、`Unregister` drain、释放渲染资源，然后停止 owner。`RenderPipeline.cpp:685–693` 的 `Unregister` 确实等待该 client 活跃回调归零。

Owner 线程由 `StartupPreview.cpp:182–235` 的 `std::thread` 拥有，先写入 `threadId`（`:184–187`），再 `RegisterClassExW`、`CreateWindowExW`、`SetWindowPos`，最后写入 `window/ready` 并进入 `GetMessageW` 循环。`PreviewWindowProc` 的 `OwnerStopMessage` 在 `:158–161` 隐藏并销毁窗口，`WM_DESTROY` 在 `:162–164` 发出退出消息。非 owner 线程只投递 `Show/Hide/Move/Stop` 命令；`PresentPixels` 与 `OwnerMoveMessage` 共用 `presentationMutex`（`:135–155`、`:272–300`）。

## 可达交错

1. 主线程 `PreviewOwner::Start` 在 `:236–239` 最多等 2 秒；owner 线程刚记录 `threadId`，在首次建立消息队列前被延迟，或处于创建窗口的长时调用中，尚未发布 `state->window`。
2. 主线程等待超时并返回 false，局部 runtime 析构进入 `PreviewOwner::Stop`。`:245–254` 看见 `window == nullptr`，只调用 `PostThreadMessageW(threadId, WM_QUIT, ...)`，未检查返回值。Microsoft 的 `PostThreadMessageW` 文档明确：目标线程没有消息队列时返回失败并报 `ERROR_INVALID_THREAD_ID`；不能凭有效线程 ID 假定投递成功。
3. `:255–261` 再等 1.5 秒，仍未见 `exited` 便 `detach` 并清掉 owner 的 `state_`。owner 线程捕获的 `shared_ptr<OwnerState>` 保住结构体，所以这里通常不是 use-after-free，而是控制权丢失。
4. 如果 owner 之后继续完成 `CreateWindowExW` 并进入 `GetMessageW`，没有停止标志可查；预览未注册 render client，也不会再有负责投递 `OwnerStopMessage` 的正式 runtime。线程可以无限等待。其 HWND 此时通常不可见，见前述样式和 show 时机；真实窗口/线程残留尚未实测。

另一个较窄的交错是 `Stop` 在 `CreateWindowExW` 成功但 `state->window` 尚未发布时仅投递线程级 `WM_QUIT`。此时队列通常已建立，`GetMessageW` 会退出；但是当前循环后没有显式 `DestroyWindow`。这主要影响 owner-thread 明确销毁合同，并不等同于同一遗漏投递缺陷。不要把所有 owner 超时都判为泄漏。

## 最小修复合同

在 `OwnerState` 增加由 `mutex` 保护的 sticky `stopRequested`。`PreviewOwner::Stop` 在决定投递消息之前先发布该标志；现有 HWND 路径仍向 owner HWND 投递 `OwnerStopMessage`，无 HWND 时 `PostThreadMessageW` 只能作为唤醒尝试，不能作为停止成功依据。owner 线程在创建前，以及创建/发布窗口后、进入阻塞 `GetMessageW` 之前检查停止标志；已经创建的窗口必须由 owner 线程自行 `DestroyWindow`，随后发布 `exited` 并通知等待者。`ready/creationSucceeded` 在停止已请求时不能报告一个可继续使用的 owner。所有共享标志与 HWND 快照仍按同一 mutex 发布，避免新裸数据竞争。

保留 2 秒启动和 1.5 秒停止有界等待；超时后可 `detach`，但只在迟到线程持有共享状态且终会观察停止请求、自行清理的合同下接受。若 Win32 创建调用永不返回，无法用本局部方案保证线程结束；此时仍记录真正的系统阻塞，不杀线程，也不谎称全部退出方式已验证。无需改变预览视觉、alpha、RenderPipeline、Draw3 或启动策略。

## 验证现状与下一步

现有 `InkeysHeadlessTests/startup_preview_state_tests.cpp` 覆盖几何、进度和 handoff reducer；`--no-window` 仍运行这些测试，但没有触及 `PreviewOwner::Start/Stop` 的 HWND 生命周期。因此已有 headless PASS 不能验证 F-010。当前任务遵守根 AGENTS 的 GUI/HWND 禁令，未运行窗口/故障注入；本记录是生产调用链静态证据，不是运行复现。

修复后最小自动验证：完整 `InkeysRepo.sln Debug|ARM64` 编译和 `InkeysHeadlessTests.exe --no-window`，再由独立 reviewer 检查“所有 Start 失败/Stop 入口都发布 sticky stop、迟到线程创建前后都检查、窗口只在 owner thread 销毁、`Stop` 不因投递失败就当成完成”的最终 diff。可考虑让一个纯状态对象被生产 owner 和 headless 测试共同调用，但不要在测试里复制一套与生产无关的正确算法。

真实验收需另行 GUI 授权和隔离进程：在仅测试构建中为 owner 创建阶段提供受控屏障，先让主线程跨过 2 秒加 1.5 秒，再释放 owner；确认预览 HWND 不残留、线程最终退出，且普通启动和快速成功 handoff 仍正常。另测首帧 2 秒超时、主动 Stop、正常 shutdown。不能用外部 `TerminateProcess` 或纯 reducer 测试替代此验证，也不得操作用户正在运行的 Inkeys。

官方 API 依据：[PostThreadMessageW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-postthreadmessagew)、[GetMessageW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getmessage)。

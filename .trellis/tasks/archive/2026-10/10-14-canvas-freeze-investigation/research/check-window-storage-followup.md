# Window / storage 后续独立检查（2026-10-17）

## Window 检查

已审 Window.cpp 的 PrepareSettingZOrder、RaiseOwnedSetting、Submit/DrainCommands、RefreshTopmost、ApplyOverlayFullscreen，以及 window_tests.cpp 的旧 11 条断言和新增隐藏 attach 用例。对照 F4 既有 A/B 原始结论，未把旧捕获通过记录当作新排序补丁通过。

新增命令只在根刷新完成后投递到 Setting 所属线程，使用 HWND_TOP、SWP_NOACTIVATE、SWP_NOOWNERZORDER，不调用 Show。最终 matches 仍检查 HWND 身份、owner、根策略、Setting 与根的 topmost band、两个画布均在 Setting 之后。退出后拒绝新排序，失败仍由原 attach 路径回滚；无接口变化。未发现必须本地修正的问题。

原三个根组断言仍无法用现有资料确定归因：

- RunWindowTests 自建 Service，ResetState 恢复 overlayTopmost=true、whiteboard=false；第一轮刷新之前没有修改此策略的调用，没有 refresh observer，也无自定义 MagnifierHost WndProc。不能归因于上一测试的 Service 原子状态残留。
- RequestTopmostRefresh 通过 completion 等待实际 Execute；Execute 调用真实 SetWindowPos(root, HWND_TOPMOST)，随后调用 ApplyOverlayFullscreen（ITaskbarList2::MarkFullscreenWindow，当前 fullscreen=false）。不存在已找到的直接 no-op 成功分支；该 Shell 调用后是否有外部窗口层级调整，现有日志没有证据。
- IsContinuousOwnerTree 验证的是全桌面 Z 序中九个成员连续。fixture 此前执行画布显隐及独立 Setting Show/Hide，owner 图正确本身不足以证明这一全局前置条件。不能据此直接删除或放松断言，也不能把 root 的 WS_EX_TOPMOST 缺失一并解释为连续性问题。
- 后续 SetOverlayTopmost(false/true) 断言未失败，提示旧现场不是所有置顶调用稳定失败；但缺少第一轮逐成员/style 快照，不能推导准确恢复机制。

后续允许 HWND 时应保留新失败采样，并在同一真实调用中采集 SetWindowPos 后、ApplyOverlayFullscreen 后的 root/owner band 和顺序，分别隔离 Show/Hide、全屏标记及外部竞争。当前八条 Setting 相关断言属于补丁覆盖目标，未运行通过；三个根组尚无已证修复。不以源码审查宣称 11 项全部完成。

## 验证边界

- Lint/格式：指定 Window.cpp 与 window_tests.cpp 的 git diff --check 退出 0。
- TypeCheck/构建：由主会话串行运行完整 solution；本检查未运行共享构建。
- Tests：本轮禁止创建 HWND，包括隐藏窗口；未运行默认窗口套件、探针或 GUI。新窗口断言只能编译，不能以 --no-window 验证其运行行为。
- 本检查当前没有修改产品/测试代码。报告供主会话汇总，不覆盖根组未定原因。

## Storage 检查

NamedMutexGuard 的 INFINITE 改为每个工作项最多等待 1000ms。Save/Load 取得锁失败在 SelectStorageTrack 之前返回 IoError，因此超时不会先写空索引、空快照或覆写旧墨迹。WAIT_ABANDONED 仍取得锁并按原路径执行；未取得锁的 guard 仅 CloseHandle，不误 ReleaseMutex。Save completion 保留目标、slotGeneration、mutationRevision 和 fileGuid；Load completion 保留加载身份。DrawingController 的既有保存失败分支将 queuedRevision 回退到 committedRevision，不会把失败的新墨迹记为已保存；加载继续沿既有有限重试和严格身份保护执行。

新回归锁住独立临时目录对应的真实命名 mutex，向真实 service 排入 Save/Load；锁仍被持有时两项必须返回 IoError 且无加载快照。随后解锁，成功路径向同一个仍运行的 worker 提交加载，检查旧 revision=1 及墨迹仍在。旧 INFINITE 实现的失败清理分支先解锁再 drain，避免红测自己永久等待。评审发现最终 front() 断言缺少空容器保护，已反馈主会话，主会话在构建前补充 !canvases.empty()；复核最终 diff 已包含保护。

实际宿主红测 storage-red-host.json 退出 1，新测试明确报告未获得两个 completion 和旧内容未保留，既有测试未新增失败；受限 runner 的 storage-red 另有正常存储失败，不作为产品红测证据。绿测与完整编译由主会话执行后汇总。

限制：1000ms 是单次锁获取预算，不是整个 CloseAndDrain 的总期限；多个排队项仍需逐项处理，底层文件 I/O 并未因此获得超时。IoError 重试预算耗尽仍保持保护，不能宣称任意外部存储故障都会自动恢复可写。新测试不创建 HWND，不证明真实 PPT/触控联动。

Storage 两文件 git diff --check 退出 0。未发现其他必须修正的本地源码问题；未运行共享构建或 GUI。

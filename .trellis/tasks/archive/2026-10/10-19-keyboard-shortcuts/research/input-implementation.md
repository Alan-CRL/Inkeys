> 最终实施说明：本文的首次unknown/首次放行描述记录初版实现；已由全范围审查修正为受桌面、访问权与前台完整性保护的启动校准。最终合同与回归以research/check-review.md和native-desktop/keyboard-shortcuts.md为准，正常可信基线的首次Ctrl+Z已覆盖。

# 输入服务实现与局部验证（2026-10-19）

## 文件与集成合同
- `Inkeys/Inkeys/Input/KeyboardShortcuts.h`：普通头，命名空间 `Inkeys::Input::KeyboardShortcuts`；六命令 bit0..5（ToggleAnnotation / Undo / RedoY / RedoShiftZ / Clear / Freeze）。`SetEnabled(mask)` 原子发布 mask+configGeneration；`GetStatus()` 仅原子读取，不持业务锁、不日志/分配。Start/Stop 由生命周期 owner 串行；PublishRoute 是单 Bar writer；TryPop 是同一 Bar consumer。
- `KeyboardShortcuts.Transport.h`：纯生产 Matcher、全原子载荷单次快照、256槽（有效255）SPSC、Bridge。业务无能力/队列饱和/同前台可信批注心跳过期时，组合归属仍消费而不提交。未知前台不新增归属；原已拥有按压的 repeat/Up 仍消费。注入事件原样放行，不改变物理票据。
- `KeyboardShortcuts.cpp`：独立 jthread 同线程安装/卸载 LL、注册/注销 CtrlAltX；WM_HOTKEY 是 X 唯一 producer。失败状态相互独立，无 LL fallback/模拟输入。配置变化退休旧注册消息；失败注销保留实际 hotkeyRegistered 与错误值，不假称原键已放行。
- `InkeysHeadlessTests/keyboard_shortcut_input_tests.cpp`：`int RunKeyboardShortcutInputTests()`；不编译/调用系统服务，不创建 HWND，不安装 Hook。

`RouteSnapshot.lifecycle` 是 owner 提供的 scope 生命周期，`Intent.serviceGeneration` 独立捕获服务代次。`Status.lifecycleGeneration` 是服务代次。sceneScope/contextGeneration 是 owner 分配的不透明稳定身份，不能使用内容 Clear 修订充当页身份。输入只复制，不访问 Draw3/StateMode/HiMsg/UI。

`GestureState.valid=false` 代表有限一次读与 producer 发布碰撞，owner 此次不得凭它取消候选。gestureGeneration 包含 CtrlUp；interruptionGeneration 包含其它终键/额外修饰/前台变化但不含 CtrlUp。两次 Delete 捕获有相同 ctrlSessionId/gestureGeneration；第二击后 CtrlUp 不改变捕获的 interruptionGeneration，业务可继续等真实回执。配置/场景失效另按各自捕获身份拒绝。

## 已知启动限制（与 root 明确确认）
低级事件没有可证明首次 Down 是真实新按还是安装前已按住后的 repeat 的位。故每个键启动为未知；观察到真实 Up 后，该键后续 Down 才能被识别为新的物理按压。修饰 Ctrl 也需该屏障。结果：安装后的首次 Ctrl 功能手势/首次使用的终键可能放行而不执行，松键再按后才可靠生效。未观察 Down 的未知闲置左右 Shift/Alt/Win 不要求全部先操作一遍，不会永远阻止 Ctrl-only；但已经观察 Down 的未知修饰键会阻止精确组合认领直到真实 Up。

不使用 GetAsyncKeyState、GetKeyboardState 线程队列快照或失败零值伪造全局 Up。这个保守策略保留了启动已按键不被中途接管的合同，但无窗口测试无法证明安装前从未送达本 Hook 的额外已按修饰键。全局 X 由系统注册消息独立识别，安装后即可工作，不受 LL 屏障限制。此限制需在整体最终说明中明确，并接受 fresh checker 复核。

## 验证证据
使用 vswhere 定位当前 Visual Studio，Enter-VsDevShell `-arch=arm64 -host_arch=arm64`，仅直接编译独立纯头测试及无 GUI runner（忽略目录 Build/keyboard-shortcut-input-check），不是替代根任务完整 solution build。

1. ARM64 cl `/std:c++20 /EHsc /utf-8 /W4 /MT /Od` 独立编译+运行纯生产测试：退出0、无编译警告。最近一次固定30000压力尝试，255提交、29745有计数丢弃、30000次仍消费；桥逻辑 P50=400ns、P95=500ns、P99=600ns、max=308100ns（含调度抖动，不是系统 Hook/下游链耗时）。
2. ARM64 service TU 单独 `/c /W4` 编译：退出0、无编译警告；没有链接/启动或安装系统入口。
3. 测试包含六组合/X唯一入口、左右Ctrl及Shift、额外修饰、先终键后Ctrl不可中途接管、Undo/Redo repeat、Del/Q repeat单次、注入 Up 不清物理记录、设置/模态/Selection/禁用、Ctrl左右延续、两Delete完整Up、CtrlUp与其它中断区别、foreground跨界消费旧Up、过期心跳暂停业务仍保归属、DWORD回绕、未知启动、255槽饱和、并发原子快照和SPSC FIFO。
4. 本 agent 不修改工程/Bar/IdtState/Draw3/Config/Setting/MouseHook/Raw/Touch，不执行完整 solution build、不启动 GUI、不 commit。根 owner 登记工程与完成整体构建、fresh check 和实际生产业务验证。

真实 RegisterHotKey 冲突、启动/停止系统API、UIPI、Win7、Office/AltGr/设备和系统光标性能均未实机执行；不可由纯测试外推。

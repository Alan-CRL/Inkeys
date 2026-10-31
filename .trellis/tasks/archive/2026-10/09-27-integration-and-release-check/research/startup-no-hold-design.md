# E02 B：无停滞启动失败的自然退出反例

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。

## 差距与边界

四个已通过的 D004/D005/D003/B002 copied-child 用例都停在真实 fatal gate，由正式 15 秒监督退场。它们不能证明正常确认提示后会及时退出。原已审 `startup-failure-boundary-harness.md` 要求的无 hold 反例仍需实现。

仅修改 `ShutdownSupervisor.cpp` 的测试协议、父测试和已鉴权 gate；Main 的四站点、正式 SetOffSignal/Arm/日志/清理保持冻结。不新增产品普通开关、环境变量或 GUI 按钮，不改任何正式业务语义。使用同一继承 parent/ack/mapping、私有 bin/镜像文件身份鉴权。

## 最小实现

- 将现有 128B observation 的 reserved DWORD 命名为 `testMode`：0 为既有 Hold，1 为 Natural；鉴权拒绝其它值，sizeof/offset 不变。它是父进程在启动前写入的不可变字段，child 只读。
- 已授权 `HoldAuthorizedStartupBoundary` 仍发布真实 gate、意图和 deadline，Natural 模式随即返回；正式默认模式和旧 Hold 测试不变。
- 新显式父 selector `--shutdown-supervisor-tests --startup-no-hold-only [D004|D005|D003|B002]`，复用 `RunStartupFailureProcessTest` 的创建与鉴权，只改变 testMode、等待与退出断言。
- 父进程持有精确 child HANDLE；在未 signaled 期间枚举顶层 HWND，只向该 child PID、标题 `Inkeys Tips`、class `#32770` 或 `Inkeys.FluentMessageBox.*` 的可见提示发送一次确认消息。Native 发 WM_COMMAND IDOK；Fluent 发 WM_KEYDOWN VK_RETURN（实际 WindowProc 调用 ActivateFocusedButton）。不使用全局输入/鼠标、SetCursorPos、Computer Use，不点击/关闭其它窗口。发送前再次核 PID/可见/类名，受控子进程存活期间 PID 不会重用；父只记录自己发送的 HWND/时刻，消息失败不能算确认成功。
- Sleep/等待每次最多50ms，完整 child 生命周期上限25秒。强杀自己的 child 只能记 FAIL；自然结果必须 observed gate/Arm、确认消息已发送、普通退出码正确（D004/D005返回1、D003/B002返回0（沿实际Main分支））且 Arm→死亡 <12秒，排除15秒强退误计绿色。
- durable sentinel保持原验证，仅证明测试文件未变，不升级UInk恢复。错误父/无继承句柄负例沿原样每轮执行。

## 验收

1. 完整 Debug|ARM64 主 Solution Build实际退出0（root独占，PATH修正，至少5分钟预算）。
2. 运行前独立review actual auth/mode/窗口过滤/上限/默认行为；四站点逐次运行，各 raw/status保留 HWND/确认tick/原Arm tick/自然exitcode/死亡tick。
3. Hold D004复验仍沿原15秒，不由新增Natural模式改变；严格Headless/相关回归。
4. Win7、自然字体/DLL/Bar注册失败和用户手动确认体验仍未验证。此反例不是现场画布卡死根因证据。

## 所有权

root唯一写ShutdownSupervisor.cpp与此设计/父账本；其余agents不修改Main/Helper。C待审设计将来接线共享getter之前需先收回此文件写入权；UI3 fixture认证另行冻结。

## 运行后观测字段修正（待新构建复验）

原 NoHold 四站点在 PE SHA256 `4AF4783992DC398B5CD5D5251F2679C9C6FF055EAC6A7D9B3871CACF324BAFB7` 上均自然通过（父suite0）；D004/D005/D003/B002的 Arm→死亡分别875/828/782/547ms，普通码1/1/0/0，不能外推Win7。原始 `e02-startup-<site>-natural-debug-arm64.*` 不删除。

复核D005时发现 `real_result` 在测试合成ERROR_GEN_FAILURE之后填写，字段实际是合成fatal的错误而不是原DLL初始化结果。现在只在同分支先保存实际activation/module成功与原错误，再供授权观察；Arm/正式error日志/提示/清理和普通返回码保持原样。上述旧自然退场证据有效于旧PE，但该字段不作为实际DLL失败证明。修补后须重新Build及D005 Hold/Natural，独立review增量；当前未预记通过。

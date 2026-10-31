# F-026 fatal Closing 测试夹具稳定化

- 日期：2026-09-29
- 目标：修复 `--draw3-parked-desktop-exit-test` 在 Release|x64 偶发输出 `[Draw3DesktopExit] PASS` 后不退出的测试活性问题，同时保留真实 `ContactInputCoordinator` Closing 红绿验证。
- 仅改 `Draw3.DrawingController.cpp` 的无窗口测试、`Draw3.ContactInput.cpp/.cppm` 的测试专用同步 hook；fatal 生产路径仍只调用 `StopFatalInputConsumer` 封闭新 admission，**不**同步 Discard 旧 route。未改 Host、Storage、Window、工程文件或正常页切换的 `DiscardUntilTerminal`。

## 定位与边界

root 的 Release|x64 隔离 CLI 曾在 `[Draw3DesktopExit] PASS` 后 180 秒不退出；没有保存该次调用栈，不能宣称已经实测唯一死锁点。我在原二进制上用各自 PID 做六轮 10–15 秒有界复现，均自然退出 0，日志 `f026-closing-x64-before-1..6.*`。源码直接显示旧夹具用 `SuspendThread(mover)` 在任意指令处暂停 Move producer，随后主线程调用 `closingInput.PublishMove` 推断 Closing；这个返回值也可因 writer latch 争用为 false，无法证明 Up 已完成 `Producing→Closing` CAS。任意暂停还可能在测试线程持有内部资源时阻碍后续 Join。该夹具本身不满足确定性与活性要求。

新的 `ContactClosePauseForTesting` 含 `entered/resume` 两个原子标志，`ContactInputCoordinator::PauseNextCloseAfterRouteClosedForTesting` 仅在测试明确安装时生效。`ContactInputCoordinatorImpl::Close` 完成 `Producing/Quarantined→Closing` CAS 之后、进入 `LockWriter` 之前，单次摘取 hook、发布 `entered` 并等 `resume`。默认 hook 为 null，生产 Close 的 route、writer latch、终态及回收逻辑完全沿用原分支；默认仅多一次 null 指针原子读取。`ResetForNextRun` 清除未使用 hook，避免跨 Host generation 留下测试指针。测试在确认 `entered` 后才调用 fatal 生产 helper，等待有界，先 `resume` 再 join，并回收测试 contact；不再使用 `SuspendThread`、`ResumeThread` 或主线程 `PublishMove` 猜测状态。所有启动的测试进程均用自有 PID；没有对用户进程执行操作。

## 红绿与跨架构验证

| 阶段 | 构建 / 运行 | 结果 |
| --- | --- | --- |
| 确定性红 | 原生 ARM64 MSBuild 完整 `InkeysRepo.sln Release|x64`，随后旧 fatal `FinishFatalInputRoute→DiscardUntilTerminal` 与新 Closing hook 的早期无窗口 CLI | Build exit 0；CLI PID 35164 **自然 exit 1**，原 Desktop PASS，唯一失败 `fatal route finish must return before Closing producer resumes`。`f026-safeclosing-red-release-x64-build.log`、`f026-safeclosing-red-release-x64.stderr.log`。 |
| 生产绿 | 同一 Solution/配置，fatal 恢复 admission-only，测试沿用同一 CAS 后暂停 hook | Build exit 0；CLI 先导 PID 64712 自然 exit 0，Desktop、FatalClosing、FatalActiveInk 三项 PASS。`f026-safeclosing-green-release-x64-build.log`、`f026-safeclosing-green-release-x64-smoke.stderr.log`。 |
| x64 稳定性 | 相同 Release|x64 二进制和 CLI 额外三轮 | PID 42908、63112、63276 均自然 exit 0，每轮三条 PASS；`f026-safeclosing-green-release-x64-trial-1..3.*`。无测试超时与强停。 |
| ARM64 Debug | 完整 `InkeysRepo.sln Debug|ARM64`；CLI；`InkeysHeadlessTests.exe --no-window` | Build、CLI、Headless 均 exit 0；`f026-safeclosing-green-debug-arm64-build.log`、`f026-safeclosing-green-debug-arm64.*`、`f026-safeclosing-green-debug-arm64-headless.*`。 |
| Win32 Release | 完整 `InkeysRepo.sln Release|Win32`；CLI；Headless `--no-window` | Build、CLI、Headless 均 exit 0；`f026-safeclosing-green-release-win32-build.log`、`f026-safeclosing-green-release-win32.*`、`f026-safeclosing-green-release-win32-headless.*`。 |
| x64 Headless | Release|x64 `InkeysHeadlessTests.exe --no-window` | exit 0；`f026-safeclosing-green-release-x64-headless.*`。 |

三个源码文件保持 UTF-8 BOM 和 CRLF；`git diff --check` 通过。冻结 blob：Controller `e1c40b657d2fcb5b332ef40b69bc110547288fac`、ContactInput.cpp `2346d87e6cc4f94d9782d3187932410c140d4b8a`、ContactInput.cppm `87e6377e9d0392fb453a7e26dd469fbdc4cdff67`。构建日志中的第三方头、C4324、C4456/C4244 等既有 warning 没有作为代码成功的替代；以上结论都以进程/构建退出码和 PASS 输出核对。

## 剩余风险与独立单元

测试证明的是真实 coordinator route 已进入 Closing 时，**fatal helper** 可在 producer 放行前返回，且三架构当前机器上的早期 CLI 正常结束。尚未注入真实 GPU fatal、RTS/COM Shutdown 卡死、Win7 真机或用户现场画布卡死；这次夹具修补也不能反推 root 曾见 180 秒挂住的唯一栈点。普通页切换/正常完成仍调用 `DiscardUntilTerminal`，它在 Closing 状态忙等，若 producer 永久不推进则有独立活性风险。不能直接把全局 Closing 分支改为 return，否则随后 producer 可转 `ConsumerOwned` 而调用者已失去 handle，slot 泄漏。普通路径的延后回收状态机和 `Close` 后置 guard、终态 CAS、`TryReadSnapshot`、Host Reset 竞态由 root 另立单元审查；本次未实施。

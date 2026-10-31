# F-060 Laser 禁多指时第二 Touch 的 slot 回收

- 日期：2026-09-29
- 依据：[实施前设计](laser-ignored-touch-slot-design.md)。文件范围只含 `Draw3.DrawingController.cpp` 的一个拒收分支、同文件无窗口生产 probe 与本记录；未改 ContactInput 状态机、Host、Idt、Setting、工程或模型/视觉。

## 问题和最小修复

`initializeStroke` 收到第二个已出队 Touch Down 时，若本批工具为 Laser、已有活动 Laser Touch 且多指开关关闭，原代码 `Recycle(handle)` 后丢弃 handle。此时路由仍为 Producing，`Recycle` 不回收；随后物理 Up/Cancel 转为 ConsumerOwned，没有消费者再持有 handle，slot 留占。修补将原四项判定和拒收动作放在同一窄生产 helper `IgnoreAdditionalLaserTouch`，生产分支与无窗口测试调用同一入口；条件为假时原 Down 路径继续，条件为真时精确 handle 调用现有 `DiscardUntilTerminal` 并返回 false。它使第二个 route 在仍 Producing 时转 Quarantined，物理 Up/Cancel 由唯一 producer 释放。第一根活动 Touch 不被碰触；若 Up 与拒收并发进入 Closing，F-057 的延后回收合同使绘制线程有界返回。本单元没有增加全局 Recycle 特例。

## 红绿证据

红版 helper 保留旧 `Recycle`，同一生产分支已调用 helper。完整 `InkeysRepo.sln Debug|ARM64` Build exit 0，早期无窗口 `Inkeys.exe --draw3-parked-desktop-exit-test` 自有 PID 57232 自然 **exit 1**：Desktop、FatalClosing、FatalActiveInk 三项 PASS，仅 `[Draw3LaserIgnoredTouch] FAIL: ignored Up/Cancel automatically retire without consuming slots`。测试使用真实 `ContactInputCoordinator`：第一 Touch 保持 Producing；32 次第二 Touch Down 经生产 helper 拒收、交替 Up/Cancel，在每次终态后先检查 `occupiedSlots/recycled`，红版再持本地 handle 人工 Recycle，故测试自身不累积泄漏。日志：`TestResults/release-hardening/f060-laser-red-debug-arm64-build.log`、`f060-laser-red-debug-arm64.{stdout,stderr}.log`。

仅把 helper 内一行改成 `DiscardUntilTerminal` 并加中文 owner 注释后，完整 Solution Build exit 0、0 error，同一 CLI 自有 PID 63076 自然 **exit 0**，四项 PASS；`InkeysHeadlessTests.exe --no-window` 自有 PID 52792 自然 exit 0。日志：`f060-laser-green-debug-arm64-build.log`、`f060-laser-green-debug-arm64.{stdout,stderr}.log`、`f060-laser-green-debug-arm64-headless.*`。随后把原 Laser/Touch/首指/开关判定也收进同一个 helper，生产与测试共用完整入口；测试再加首指尚不活跃、`enabled=true`、实际 Pen 非 Touch、非 Laser Touch 负例。最终完整 Solution Build exit 0、0 error，同 CLI 自有 PID 34208 自然 exit 0，四项 PASS；日志 `f060-laser-final-debug-arm64-build.log`、`f060-laser-final-debug-arm64.{stdout,stderr}.log`。这一最后改动只移动原有布尔判定，不放宽任何业务条件。

隐藏 Host `--draw3-hidden-test` 在受限沙箱内自有 PID 46892 自然 exit 1，日志先有多次 PPT `io_error`，随后 DComp/Legacy 各出现 Selection/end-page held-contact 断言失败；`f060-laser-green-debug-arm64-hidden.*`。同一 Debug|ARM64 可执行文件在沙箱外以测试自身隔离 persistence root 运行，自有 PID 50220 自然 exit 0，`PASS: real Host held-contact save, drain, cold reload and SlideID reorder`、`PASS: all hidden integration checks`；`f060-laser-green-debug-arm64-hidden-unsandboxed.*`。这与既有隐藏 Host 测试在沙箱内文件替换失败的证据一致；没有改产品源码来绕过环境。最终完整 predicate helper 移动并冻结源码后，**再次**用同一有界隐藏 Host 入口、正常隔离文件访问复验，自有 PID 42336 在约 21.5 秒自然 exit 0，两条 Host PASS 均存在；原始日志 `f060-laser-final-debug-arm64-hidden-unsandboxed.{stdout,stderr}.log`。未触发超时或强停。

## 回收调用审计与边界

- Controller 同文件其它 `Recycle`：无窗口 ingress probe 先读到 Up/Cancelled；pan handoff 仅 terminal 分支；reconnect 旧 handle 已实际 Up；三处失败回退先同步 `PublishCancelled`（并发已有 Closing 由 F-057 处理）；gesture runtime 仅 terminal snapshot 时回收。只有本次 Laser 第二 Touch 分支在仍 Producing 且丢 handle 时直接 Recycle。
- 生产产品路径的 Laser 工具选择和真实双指输入未以 GUI/硬件动态注入；无窗口测试直接调用与 `initializeStroke` 共用的完整 predicate+回收 helper，再由源码核对该唯一调用点。测试覆盖32轮第二 Touch、第一根持续可读/被 admission 承认、第二槽同址新 generation、Up/Cancel 自回收、Closing hook 暂停下有界返回，但不能冒称实机激光轨迹/粒子画面通过。
- Release|x64/Win32/ARM64、Win7、真实 RTS 多点竞争及用户报告卡顿栈未由本单元完成；等待冻结版独立 diff review 与 root 统一构建槽安排。源码 Controller 冻结 blob `6b77b771c45479a5a35a65c07280853fcd8c752a`，UTF-8 BOM+CRLF 保持，`git diff --check` 通过；未 commit/push。

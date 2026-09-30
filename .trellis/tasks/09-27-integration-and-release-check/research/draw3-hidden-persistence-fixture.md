# Draw3 隐藏窗口测试的持久化夹具

## 2026-09-29 调查与最小设计

- 红证据：`TestResults/release-hardening/hf-final-rel-arm64-draw3-hidden-test-unsandboxed.stderr.log` 从首次 PPT A 切换起连续出现 `action=load_submit result=failed`，随后 `A starts with its independent empty canvas`、墨迹、Clear/Undo/Redo 和页面身份断言失败。该运行由主任务在约 7 分钟时终止，不能写成自然退出或确定的总失败数。沙箱内首轮则 180 秒超时。
- 真实调用链：`RunMode` 创建 `HostStartOptions`，命令模式未填 `autoSaveRoot`；`Host::Start` 仅在非空时启动 `PresentationAutoSaveService`，`ObservePresentationLoad` 因 `SubmitLoad` 返回 Closed 拒绝；F041 Controller 正确地阻止未加载的 PPT 输入。产品 `IdtMain.cpp` 设置 `GetCurrentExeDirectory() + "\\Inkeys\\AutoSave"`。独立 `CheckPresentationPersistence` 已自建唯一 root，不能代替其前一段 `RunMode` 的持久化 worker。
- 改动边界：仅 `Draw3.HiddenWindowTest.cpp`，只在 `exerciseCommands` 为真时、Host 启动前创建测试进程独有的 root，放在仓库忽略的 Build 产物目录，显式拒绝 reparse 目录。保留真实 worker、Controller、Host、输入和所有断言；不改产品失败策略或无限加长超时。
- 验收：同一完整 Solution 的 Debug|ARM64 与 Release|ARM64 构建，Release 隐藏窗口 CLI 自然退出码 0；同时复核相关 eraser hidden 和 no-window 测试。失败若转为另一类，单独记录首错，不把夹具假设当作证明产品无问题。

## 实施与复验

- 仅改 `Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp::RunMode`：在 `exerciseCommands` 为真时、`StartProduct` 之前，在 EXE 所在的仓库忽略 `Build/` 下创建 `Draw3HiddenPptCommands/<PID>-<QPC>`。容器和唯一目录均检查目录属性与 `FILE_ATTRIBUTE_REPARSE_POINT`，且已存在的同名唯一目录拒绝复用。把该 root 交给真实 `HostStartOptions`；原测试断言、worker 与 fail-closed 规则均保留。每轮 root 完整打印到 stderr，供复核 UInk/index。
- `git diff --check -- Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp` 退出 0；该文件仍是原始无 BOM UTF-8，所有 2620 个换行仍为 CRLF、孤立 LF 为 0。初始文件已有其他阶段改动，因此 `git diff --numstat` 不是本次夹具的新增行统计；本次补丁只加上述条件块。
- Debug|ARM64 完整 `InkeysRepo.sln /t:Build /p:LinkIncremental=false /m:1` 退出 0；`TestResults/release-hardening/draw3-hidden-fixture-debug-arm64-solution.log`。Release|ARM64 同配置退出 0；`draw3-hidden-fixture-release-arm64-solution.log`。二者均使用当前 VS 的 ARM64 原生 MSBuild、同一 PowerShell 中规范 `Env:PATH`。
- 默认沙箱 Debug 新测试 PID 63592 自然退出 1；`draw3-hidden-fixture-debug-arm64-test.stderr.log` 已不再出现旧 `load_submit Closed`，而是真实保存 worker 报 `io_error`，两轮各在 `Selection returns to Z before held-contact exit` 和 `end-page held contact accepted` 失败。目录里可见部分 index/UInk；不能把此运行记为 PASS，也不能仅凭它判定产品有逻辑错误。
- 同一 Debug 二进制在沙箱外仅用隐藏 HWND 与隔离 Build root 运行，PID 64596 自然退出 0；`draw3-hidden-fixture-debug-arm64-unsandboxed.stderr.log` 两轮各有 `PASS: real Host held-contact save, drain, cold reload and SlideID reorder`，最终 `PASS: all hidden integration checks`，无 `load_submit result=failed`、`io_error` 或 `FAIL`。因此上述沙箱内 I/O 错误属于当前 runner 验证环境限制，保留原失败日志。
- Release|ARM64 沙箱外同一测试 PID 10396 自然退出 0；`draw3-hidden-fixture-release-arm64-unsandboxed.stderr.log` 同样两轮真实持久化往返、最终全部断言 PASS。
- Release|ARM64 `--draw3-eraser-hidden-test` 首次 PID 61932 自然退出 1，四个 UInk 边界断言失败。原因是运行命令把工作目录设为 EXE 目录，而该现有测试 `CheckSizeBoundaryFile` 用相对 `Build\\eraser-size-boundary-*.uink`；与本次 root 补丁无关。工作目录改为仓库根后，原测试 PID 50132 自然退出 0、`PASS: DIP eraser actual cursor, idle scheduling, geometry footprint and Undo/Redo`；红绿日志分别为 `draw3-hidden-fixture-release-arm64-eraser.stderr.log` 与 `draw3-hidden-fixture-release-arm64-eraser-reporoot.stderr.log`。没有修改断言。
- Release|ARM64 `InkeysHeadlessTests.exe --no-window` 退出 0，`TestResults/release-hardening/draw3-hidden-fixture-release-arm64-headless.log` 末尾 `PASS animation correctness`、橡皮相关 failures=0。

## 边界

该测试仍是隐藏 HWND、合成输入与本机持久化 worker 回归。真实 Office COM、笔设备、Win7 SP1+KB2670838 的 DComp/ULW、硬件 FL11.0 或 WARP 组合仍需目标机器验证。默认沙箱保存 worker 的失败不能覆盖沙箱外自然通过的结论，也不能反向推出所有真实磁盘错误均已正确恢复。

# E02 C：失败清理 helper 实施交付

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。实施者：`failed_cleanup_impl`。当前阶段：**C-P1 真实 helper 已交 PATCH_READY / 绿色构建和运行由 root 待执行**。下文 RED 接口与散列保留为红测历史，最新源码身份见末节。

## 边界与设计身份

- 冻结合同 `failed-cleanup-lifetime-contract.md` SHA-256：`4A7EDE9C5C44AEF068F892BDC489B3203D270D1A10A8288CCEA577D16CCBF683`；独立 `failed-cleanup-lifetime-design-review.md` 为 GREEN_DESIGN，不代表实现或运行通过。
- C-P1 首先交 RED 无监督接口，root 已实际取 expiry/allocation 红证据并发 GREEN_IMPLEMENT；现已交真实 helper 源码，等待 root 绿色构建、primitive 和独立 review。C-P2 模块接线仍未授权。
- 唯一修改为新的 `FailedCleanupDeadline.h/.cpp` 和本报告。Main、ShutdownSupervisor、工程、公共测试、Window/Host/RTS/presenter、Controller/RuntimeMetrics、Bar、spec 与父账本均未修改。共享工作区的 E01/E02 A/B/E03/UI3/U1 现有编辑保持原样。

## C-P1 RED 接口与行为（历史基线）

- `FatalCleanupPublisher = ULONGLONG (*)() noexcept`，没有 void* context，也不依赖 Main、ShutdownSupervisor 或产品 module 符号。
- `FailedCleanupSignal` 按值持有独立 shared State；`BeginKnownFailure(ULONGLONG inheritedGraceDeadline=0)`、`HasPublisher()`、`Publisher()` 和 `FailUnprovenProducerStop()` 签名齐全。
- scope 的 `PrepareOrFatal()`、`Signal()`、`CompleteOrFatal()`、析构齐全，显式禁用 copy/move；constructor 第二参数默认 `{}`。
- `FailedCleanupTestGates` 是固定 trivial/standard-layout POD：两个失败 bool 和六个 HANDLE，名字与 root 冻结完全一致。门按值保存在 State 中，不存业务对象，也不关闭借用测试事件。默认 `{}` 全零；只能由验真 child 测试 caller 提供有效门。
- **RED 行为**：Prepare 可建立固定共享 State，但不创建 wake event 或 monitor；state allocation fault/实际分配异常不接管；monitor creation fault 未启用。Begin 恒返回 0，Complete 正常返回且不调用 publisher/强退。测试事件此阶段仅存储，不触发。noreturn producer-stop 占位永远 Sleep，由 parent 上限后精确清理自己的 child。
- 当前 helper 未接任何正常产品调用点，不能部署为已提供清理截止保护的产品。没有取消/过期 CAS、真实 HANDLE join 或专用强退码运行证据。

## 验证与交接

- 已读真实 AGENTS、保存的完整 hook、实际 implement.jsonl/PRD/design/implement、冻结合同/复审、build/errors/resources/input/Draw3/C++/quality 与共享指南、父 handoff/人工清单；get_context packages 确认单仓库。
- 已搜索 helper 符号与 `0xE143001A/B/C/D` 的现有源码引用；实施前没有同名 helper 或专用码冲突。POD 编译 static_assert 已加入，但尚无编译结果。
- 两个新源码与本报告均为严格 UTF-8 无 BOM、纯 CRLF，无 lone LF/CR。源码分别 65 行/2098 B、83 行/2393 B。`git diff --check` 退出 0；三个 untracked 文件各用 `git diff --no-index --check -- NUL <file>`，退出 1（新增 diff 的预期 no-index 状态）且检查输出为空，无 whitespace error。共享其它作者文件的 Git EOL 警告未作修改。完整 `InkeysRepo.sln Debug|ARM64`、Headless、CLI/copied-child 与所有 GUI/root 输出目录由 root 独占；本 agent 未执行，不预填 PASS。
- 后续先 root 登记新源/头、静态 publisher 和真实 copied-child RED；获 GREEN_IMPLEMENT 后才实施 shared State/wake 最后引用关闭、单字 CAS、Dormant、真实 join、失败 noreturn 与原普通截止取 min。模块调用点属于 C-P2，须另获 root 接线指令；AutoSave 后续合同另冻。

## C-P1 RED 源码冻结（历史）

- `FailedCleanupDeadline.h` SHA-256：`7B01FBC0F4831303CC1188E2E71A0A44C3D9CAE00EB20549CA4B26D6C94B428F`。
- `FailedCleanupDeadline.cpp` SHA-256：`01D80F65426CBA971C78961D3AF81C2D34817514F0240C789A4A78A8D31D0FC7`。
- 已发 HARNESS_READY。所有 RED 文件停写；下一阶段需 root 的真实红测证据与 GREEN_IMPLEMENT。此冻结只证明源码身份、接口与静态格式检查，没有编译或动态 PASS。

## C-P1 GREEN_IMPLEMENT 后的源码交付

root 已明确授权仅实施两个新 helper。已只读核对 `failed-cleanup-harness-design.md`、最新 96B caller、Main 的进程寿命静态 publisher/getter 和真实 RED 原始日志；没有修改其所有文件，也未接 Window/Host/RTS/presenter。

### 已写行为

- scope、每个按值 Signal、monitor envelope/入口都持自己的 State 强引用。State 只保存固定原子、静态 publisher、按值测试门和 wake HANDLE；测试事件仅借用，不关闭。monitor HANDLE 仅 scope 管理；真实 join 成功后才关闭，wake 仅 State 最后引用关闭。
- `atomic<ULONGLONG>::is_always_lock_free` 是编译门。唯一 control 字为 Dormant 0、Cancelled 1、Armed 绝对 tick、Expired 原 tick 加高位。Begin CAS 发布完整 deadline，重复 Begin 不延长；empty/旧 Cancelled 返回 0。非零 inherited tick 取更早值，tag/溢出拒绝并进入 fatal。
- monitor 在 Dormant 无时间限制地等 auto-reset wake，Armed 按剩余绝对时间等待；Begin 和取消显式 SetEvent。Cancel 赢后等真实 monitor HANDLE，1000ms 管理 join 成功才返回；没有取消轮询、detach 或 TerminateThread。
- Complete 已看到 clock 到期时只竞争 Expired，不能取消；Expired 将原 grace tick 同 CAS 发布，所有接管者复用 `grace+15000`，迟到 publisher 不重加时间。Prepare、join/唤醒/等待管理失败、producer 停止未证实按首次接管 tick 加 15000。共同 fatal tick 在 publisher 前原子保存，随后只可缩短；publisher 返回已有普通/UEF tick 时取 min。强退循环每最多 50ms 重读可能缩短的共同 tick，这只发生在 fatal 阶段，不给正常取消增加等待。
- 无 State 的 OOM 路径只用已保存函数指针和栈上 tick，不再创建线程/事件、分配或等待业务。CreateEvent/CreateThread/envelope 失败也当前线程不返回；创建失败的固定资源保留至进程死亡。join 超时/失败不关闭未 signaled monitor，不释放 State 或业务调用栈。
- 退出码：`001A` cleanup-expired，`001B` prepare-failed，`001C` cancel/join/wake/wait 管理失败，`001D` producer-stop-unproven，前缀均 `0xE143`。最终只走 GetTickCount64/Sleep/TerminateProcess 的不返回循环，意外自终止返回继续重试，不执行 ExitProcess/析构。
- `afterBeginClaimed/continueBegin` 仅在 Begin 赢 CAS 后、SetEvent 前；方法局部强引用保持旧 wake，即使 Complete 已取消并 join。`beforeExpiredClaimed/continueExpired` 仅暂停 monitor，Complete 不经过此门，仍能独立赢 Expired 并接管；否则 root 的 expiry-cancel 会被错误阻塞。`monitorCancelExitEntered/continueMonitorExit` 在真实 Cancelled thread 退出前，证明 join 超时面对的是真实活线程。
- scope Prepare/Complete 生命周期由同一管理 owner 串行调用；跨 owner 使用按值 Signal。公共接口和两个 bool/六个 HANDLE 均未扩充。

### RED 证据与当前验证界限

以下是 root 运行、本实施者随后只读核对的 RED；不是本实施者运行，也不证明新 helper 已通过。

- `TestResults/release-hardening/c-p1-ui3-b1-red-debug-arm64-build.status.txt`：完整 Debug|ARM64 build `exit=0`。
- `c-p1-expiry-red-debug-arm64.status.txt/.stdout.log`：parent exit64/PID27400；child17348 authorized/activated，grace=0/published=0，约40秒仍 alive，parent 只清理自己的精确 child，记 FAIL。wrong-parent/no-inherited 分别拒绝84/83，PASS。
- `c-p1-allocation-red-debug-arm64.status.txt/.stdout.log`：parent exit64/PID24364；child30380 已授权，Prepare/Complete 返回 after_complete=1、自然0、无 publisher，记 FAIL；两个鉴权拒绝 PASS。
- root 增加的 cancel-next-scope、begin-cancel、expiry-cancel、auth-only 更早6秒 tick 已按实际 caller 静态核对；运行需要这次新构建及独立 safety/review。ordinary 集成例可能由原监督结束，本身不能独立证明 helper 的 min。
- 本实施者没有 MSBuild/EXE/GUI/性能采样。helper 源码和格式检查通过不升级为动态 GREEN。全部 primitive、Headless/PptCOM、三架构 atomic/link、最终实际 diff 独立复审仍由 root 执行。

### 最新源码身份与未覆盖范围

- `FailedCleanupDeadline.h`：UTF-8 无 BOM、68 CRLF 行、2392 B，SHA-256 `1D8DB0010A388CC21522F1D0F9CC7858CB8D59DFCB928431C6C3112A94AAD0A0`。
- `FailedCleanupDeadline.cpp`：UTF-8 无 BOM、293 CRLF 行、11417 B，SHA-256 `95932CE0ECBCEE120F884FD834A9A72E3A85479A296420CD6D87BF9239F37F4C`。
- 两源码无 lone LF/CR；每个 untracked `git diff --no-index --check -- NUL <file>` 为预期 diff 状态1、检查输出为空。整体 `git diff --check` 为0。仅这两个源码和本报告由本 worker 修改，无业务文件或工程接线。
- 已交 PATCH_READY，源码停止写入。C-P2 显式已知失败调用点、正常 ULW 回退、RTS 停止证明、真实 Host/render/Desktop/PPT 停滞及 fresh UInk 恢复尚未实施/验证；helper 不能替代它们。未返回的 OS/COM/driver 调用、进程不被调度以及目标 Win7 真机仍是原合同的独立边界。

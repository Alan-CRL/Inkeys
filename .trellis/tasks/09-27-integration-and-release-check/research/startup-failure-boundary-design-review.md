# E02 启动失败边界设计独立审查

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。仅审设计与当前生产调用链；未实施、编译或运行新夹具。本文件由独立 checker 写入，产品和共享账本归各自 owner。

## 分批结论

- **A 可实施**：`IdtMain.cpp:172–193` 的 `PublishFatalStartupFailure` 目前在 Report/失败帧/Show 之前没有 `SetOffSignal`；早期 D001/D002 (`1550–1564`) 直接 Report+Show，D004 (`1775–1785`) 经 helper；D005 (`2155–2169`)、晚 D002 (`2182–2190`)、D003 (`2214–2228`) 的日志又在 helper 前。先把正式 fatal 意图置于所有潜在阻塞的日志、提示和清理之前，保留原分支返回码与提示/失败帧合同。D005 **先保存 GetLastError** 再 Arm，否则记录的 Win32 错误可能变成监督器调用的错误。`ActiveSnapshot.failed` (`2743–2757`) 也要在等帧与字符串构造前 Arm。B001/B002 的真实 Bar 失败发布者可沿该通路复验；B004 的三处 `StoppedBeforeReady` 已以非零 offSignal 为前置，不能把它写成已证实的未受保护生产来源。最小 A 不需要 C 的跨模块接口。
- **B 可分段实施**：先做 D004/D005/D003/B002 的受控真 `wWinMain` 夹具和红→绿，再扩充其他分支。旧逻辑红灯须观察目标失败已发布、实际 gate 已到、意图尚未 Arm 且 child 仍活；绿灯须由产品自己的 15 秒监督令精确 child HANDLE 死亡。父进程到上限后自清理不能记绿。默认路径和正式应用无可触发故障开关。`B002` 人工发布真实失败状态只能证明 Main 处理合同，不冒充 Bar 注册自然失败。
- **C 暂不批准写跨模块源码**：失败清理 lease 的思路合理，且适用边界是 **已知启动失败** 后的 Host 内部 join/drain、Window rollback/owner join 与 DComp→ULW 旧 generation 清理。`Host.cpp:1234–1250,1278–1297` 已在 `Start(false)` 返回前做失败清理；后续 `StopProduct` 通常不再进入活动 Host 保存屏障。普通已 Arm Close/Restart 的 `Host::Stop` 无界 drain 由现有进程 15 秒监督兜底，不能把它另报成此设计确认的未保护启动死锁，也不应给正常保存路径添加任意短超时。

## C 实施前必须冻结的合同

1. **scope 生存期与 no-return 失败路径。** 提议的 `FailedCleanupCallbacks.context` 是借用 Main 栈上 scope。设计写 monitor 1000ms join 超时后“保留 context/句柄/业务资源直到退场”，但 `~FailedCleanupDeadline`、`CompleteOrFatal()` 的拟议签名尚未证明不会返回并析构这块栈内存。Expired、Prepare 失败、Cancel-join 失败都必须明确进入不返回正常流程的进程级退场；或把监视状态放进可信进程寿命对象。不能返回 false 后继续 ULW，更不能 detach 后销毁 callback context。
2. **取消与 expiry 的线性化。** 同一 CAS 决出 Cancelled/Expired，过期赢后不允许 `CompleteOrFatal` 返回第二次 Start。正常 DComp 失败且清理在 deadline 前结束时，监视者必须完成 join/解除，不在下一代 ULW 运行时迟到强退。若 1000ms 管理 join 失败，必须保证已有产品监督真实建立；不能只调用可能返回 Failed 的 `SetOffSignal` 就再次进入清理。参考 E01 的 Failed-only noreturn 接管合同。
3. **最早失败点与回退范围。** `Host::Start` 的 graphics/RTS 已知失败发生在其内部清理前；Window 的 `created` 抛错/rollback 可能在 `Start` 的 promise 发布前。lease 需要进这些真实 owner 的失败边界，不能只在 Main 收到 false 后开始。正常、尚未返回的 `CreateWindowEx`/COM 初始化不在“已知失败清理”承诺里；报告中如实标不可覆盖。
4. **停止 RTS 不等于 producer quiescent。** `RealtimeStylus::Shutdown` 若 Disable/Remove 失败，不得以 watchdog 存在就复用输入槽或释放仍可回调的对象。C 的 Host/Window/RTS 接口写入者先由 root 明确唯一分配，含 module/public interface 变更单独复审。`HostStartOptions`、Window Start 的默认空 callback 不改变旧 demo/普通产品的行为。
5. **预算语义。** 提议的 15 秒是从首次已知失败开始的清理 grace，再接受 Fatal Close 进入用户已批准的进程级 15 秒强退；约 30 秒上界只适用于该失败路径，不能写成点击 Close 后 30 秒。所有子步骤共用第一次的绝对截止。保留 Win7 SP1 仅 KB2670838 可用的 Win32 原语、FLIP_SEQUENTIAL 和仅 DComp/ULW。

## 夹具与验收边界

私有 child 须复用 E01 复制 EXE、精确继承父 HANDLE/镜像文件身份、固定 POD 映射鉴权；新映射不能混用 E01 旧 48 字节 schema。无授权或坏句柄应在配置、单实例、窗口前失败。复制目录本身不足以隔离真实系统副作用：`IdtMain.cpp:2056–2057` 的启动注册表状态、`:2117–2120` shortcut/Desktop blocker、SuperTop、PPT 线程和发布宏更新线程须仅在鉴权 child 跳过并逐项记录覆盖限制。不得接触用户已有 Inkeys/Office/文档，也不得使用 computer-use。

先取真实 D004/D005/D003/B002 红→绿与取消/无 hold 反例。C 独立验证实际 Window rollback、Host 失败 join、RTS 清理、成功 DComp→ULW 首帧且等待超过原 grace 仍活，以及已 Arm 的绘制/保存 worker hold 后 15 秒死亡与**最后已提交 UInk/index** 的生产 codec 恢复。正常 Save 仍需无故障自然排空。每项记 PID、site、故障身份、首失败 tick、Arm/expiry/死亡 tick、退出码及实际产物；不把 sentinel 当数据恢复。

## Verification

已只读核对 `check.jsonl` 引用、PRD/design/implement、父 handoff、E01 审查和 `IdtMain`/Host/Window 相关源码。Lint/TypeCheck/Build/Tests：本轮设计审查未运行；A/B/C 均未记实现或动态 PASS。

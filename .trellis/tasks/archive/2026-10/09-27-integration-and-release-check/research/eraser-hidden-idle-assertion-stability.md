# Win32 隐藏橡皮 idle 断言的时间基线

日期：2026-09-29。最终 Release 三架构隐藏矩阵的 Win32 `--draw3-eraser-hidden-test` 首轮自然 exit1，唯一 `[Draw3Hidden] FAIL: idle does not rewrite historical width or submit fake points`；同一 Win32 可执行文件隔离复跑自然 exit0。原始日志 `hf-code-freeze-Win32-draw3-eraser-hidden-test.stderr.log` 与 `hf-code-freeze-Win32-eraser-repeat.stderr.log` 均保留。一次失败/一次通过只确认测试不稳定，**尚不能把根因断言为产品重写点或 F-057/F-060 回归**。

`Draw3.HiddenWindowTest.cpp` 的高速 80 个 Move 使用 `PostMessageW`，循环结束立即取 `large = ProductHost().RuntimeSnapshot()` 并将 `large.inputMovePublished` 和 `large.eraser.realPointCount` 作为随后「停止所有 Move」的基线。`PostMessageW` 成功仅表示消息已入 owner 队列；Owner→Host PublishMove→绘制线程模型消费可能晚于此快照。现测试已经等待 idleSeconds≥1 与几何直径缩小，再以**较早**的 pointCount 比对；若最后一条真实 Move 在基线之后进入，断言会误判为 idle 伪造点。首次失败只给合并布尔结果，没有打印哪个子条件，所以上述仍是需验证的时序假设。

最小测试修正只在真实等待到 `idleSeconds≥1` 和缩小条件后，取当时的 `quiet` 为无输入基线；先核 `historyRadiusPx > nextRadiusPx*1.5` 证明历史几何未被缩小。再利用现有 250ms 安静窗口的 `after` 快照核 `inputMovePublished` 和 `realPointCount` 与 `quiet` 完全相同，并保留原帧停止断言。这样只把「不再新增点」的区间定义在**已观测 idle 以后**，不取消、放宽任何历史几何/伪点要求，也不改渲染、输入采样、等待上限、窗或动画。若仍失败，分别记录两个计数与历史半径，追真正迟到输入/模型变化；不能靠延长 timeout 掩盖。

先核此文件编码/CRLF、当前所有同类 idle 断言与真实生产 snapshot；只改这一处测试、用原格式。完整 `InkeysRepo.sln Release|Win32` Build 后同一隐藏 CLI 至少三轮串行自然退出，ARM64/x64 同入口各一轮，保留首次失败与重复结果；独立 reviewer 看实际 diff 与测试意义。测试仍是隐藏合成输入，不代表真笔或 Win7 性能。

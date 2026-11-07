# 实施计划：激活抬笔与落笔屏蔽

1. 复核 design.md 列出的行号与 `completeModelUp` / `DiscardUntilTerminal` / 续接匹配实际行为；如与设计不符，回到规划说明冲突。
2. 抽出 `PalmExclusionContains`，`PalmNeighborBlocked` 改用它；原有 Palm 用例保持通过。
3. 提取收尾 lambda `finishInterrupted`，原并行关闭循环改调（行为等价）。
4. 激活帧登记 + 范围内 Touch 收尾（2.2）。
5. 独立 Down 屏蔽 + 续接前屏蔽（2.3）。
6. 文案修订与 i18n sync/check（2.4）。
7. 测试：在 `Draw3.DrawingController.cpp` 的生产 Palm probe（`--draw3-palm-eraser-test`）与 `InkeysHeadlessTests/palm_eraser_tests.cpp` 补用例：
   - 激活帧范围内/外 Touch 触点；范围内笔、鼠标不受影响；
   - 截断后 Move 不出墨、真实 Up 回收、续接不复活；
   - 激活后手掌靠近书写触点不截断；
   - 范围内新 Touch Down 隔离至 Up（含移出）；范围外正常；
   - 被拒 Down 不续接普通笔划；手掌自身续接成功；
   - 并行关闭路径回归。
8. 验证：ARM64 原生 MSBuild 完整构建 `InkeysRepo.sln` Debug|ARM64（≥5 分钟超时，处理 PATH 冲突）；`InkeysHeadlessTests.exe --no-window --palm-eraser-only`、`Inkeys.exe --draw3-palm-eraser-test`、`--no-window` 全量；i18n check。实机（大屏多触点）列为未测。
9. 检查 diff 最小、编码/EOL 保持；不提交除非用户授权。

回到规划的条件：续接匹配无法在不改 ContactInput 的前提下排除普通续接；或 `completeModelUp` 后真实 Up 不能正确回收。

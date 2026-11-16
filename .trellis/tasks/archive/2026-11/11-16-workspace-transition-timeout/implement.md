# Implement：工作区切换失败预算

## 执行清单

1. `Inkeys/IdtState.cpp`：新增 `WorkspaceFailureKind`、预算常量、`WorkspaceFailureClock`；`WorkspaceHandoff` 以 `failureClocks` 取代 `failures`，改写 `Request/Fail`，新增 `NoteProgress`。
2. `WorkspaceWaitTracker::Stalled` 改为三态返回（逻辑不变）。
3. `FailWorkspaceRequestImpl/FailWorkspaceRequest` 增加 `kind`（及可注入 `now`）；新增 `NoteWorkspaceProgress`。
4. `StateMonitoring`：按 design 映射表更新 6 个失败点与 2 个原生进展点、4 个等待点。
5. 关键处补中文注释（类别独立、为何不全局清零、退避夹紧到判定时刻）。
6. 更新 headless 用例（`RunStateModeProductionTest` 的 `workspace-handoff-production` 段）：
   - 改写 “entry-and-exit-have-finite-native-attempts” 为时间预算（AC1、AC5）。
   - “failure-notice-edge-deduplicates-after-four-real-failures” 改为注入时间跨过 15 秒后单边沿（AC1）。
   - 新增 Wait 进展重计时（AC2）、Prepare 进展不清 Complete（AC3）。
   - `WorkspaceWaitTracker` 用例改断言三态（行为不变）。
7. Phase 3：同步 `whiteboard.md` spec。

## 验证命令

- 完整构建（ARM64 原生 MSBuild，超时 ≥5 分钟）：`MSBuild.exe InkeysRepo.sln -m -p:Configuration=Debug -p:Platform=ARM64`
- Headless：`Inkeys.exe --state-mode-production-test`（期望退出码 0 与 `[StateModeProduction] PASS`）
- `git diff --stat` 与编码/换行检查（`IdtState.cpp` 保持原 BOM/CRLF 状态）

## 风险与回滚点

- 风险文件：`Inkeys/IdtState.cpp`（状态线程核心）。任何失败点漏改类别会导致编译错误或计时串类，评审时逐一核对 6+2+4 个调用点。
- 回滚：`git checkout -- Inkeys/IdtState.cpp`。

## Start 前检查

- prd/design/implement 已获用户批准。
- `implement.jsonl` / `check.jsonl` 已含真实 spec 条目。

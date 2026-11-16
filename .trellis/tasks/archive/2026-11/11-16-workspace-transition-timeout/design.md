# Design：工作区切换失败预算

## 边界

只改 `Inkeys/IdtState.cpp` 内的协调状态（`WorkspaceHandoff`、`WorkspaceWaitTracker`、`FailWorkspaceRequest*`、`StateMonitoring` 调用点）及其 headless 用例。Window 服务、Draw3 Host、Setting 通知入口、i18n 文案均不动。

## 数据结构

```cpp
enum class WorkspaceFailureKind : std::uint8_t { Wait, NativePrepare, NativeComplete, Count };
inline constexpr auto WorkspaceNoProgressBudget = std::chrono::seconds(15);
inline constexpr auto WorkspaceRetryBackoffCap = std::chrono::seconds(2);

struct WorkspaceFailureClock
{
    std::optional<Clock::time_point> failingSince; // 本类别自上次进展后的首次失败
    unsigned attempts = 0;
};
// WorkspaceHandoff 内：std::array<WorkspaceFailureClock, Count> failureClocks; 取代 unsigned failures
```

## 契约

- `Request()`：清零全部 `failureClocks`、`retryAt`、`failed`（同现状清零 `failures`）。
- `Fail(expected, kind, now)`：`!pending || revision != expected || failed` → false（不变）。
  - 首次失败记 `failingSince = now`，`++attempts`。
  - `now - failingSince >= 15s` → `failed = true`，返回 true（唯一通知边沿，不变）。
  - 否则 `retryAt = min(now + min(250ms << (attempts-1), 2s), failingSince + 15s)`，返回 false。
- `NoteProgress(expected, kind)`：revision 匹配且 pending、未 failed 时清零该类别；不动 `retryAt`（已安排的退避照常，避免进展通知加速重试）。
- `Ready/Commit/Retry` 不变。

## WaitTracker

`Stalled(...)` 改为返回三态 `WorkspaceWaitVerdict { Progressed, Waiting, Stalled }`，判定逻辑与 250ms 节拍不变：字段/阶段变化 → `Progressed`；未满 250ms → `Waiting`；满 250ms → `Stalled`。调用点：`Progressed` → `NoteWorkspaceProgress(revision, Wait)`；`Stalled` → `FailWorkspaceRequest(revision, Wait)`。

## StateMonitoring 调用点映射

| 位置 | 现状 | 改后 |
|---|---|---|
| 4404 `NoteExecutingTimeout` | `FailWorkspaceRequest(rev)` | 按 `nativeOperation->completing` 取 Prepare/Complete |
| `nativeFailed(phase)` | `FailWorkspaceRequest(rev)` | 增加 kind 参数；Prepare 段调用传 Prepare，Complete 段传 Complete |
| Prepare `Committed` 且 `CanConsume(false)` 且 `prepared` 有值 | — | `NoteWorkspaceProgress(rev, NativePrepare)` |
| Complete `Committed` 且 `CanConsume(true)` | — | `NoteWorkspaceProgress(rev, NativeComplete)` |
| 4557/4593/4606/4634 等待点 | `Stalled` 布尔 | 三态处理（见上） |

`FailWorkspaceRequestImpl` 增加 `kind` 与 `now`（默认 `Clock::now()`）参数，供 headless 注入时间；新增 `NoteWorkspaceProgress(revision, kind)` 只持 `workspaceHandoffMutex`，不进入 UI。

## 取舍

- **按类别而非全局清零**：全局清零会让“Prepare 成功 / Complete 失败”无限循环重复显示/隐藏，违背 spec「普通失败停止无限重复显示/隐藏」。
- **时间预算而非次数**：用户诉求是“总共约 15 秒”；次数制在退避上限下难以精确对应时间。
- **不扩展进展口径**：保持“实际输出/内容/背景进展才重置等待”（whiteboard.md:83）；冷加载靠 15 秒预算覆盖。
- **已知残余**：等待阶段若在两个阶段间来回振荡，每次振荡都算进展，理论上可无限等待；与现状一致（现状振荡也重置跟踪器），不在本任务处理。

## 兼容与回滚

纯内部常量与私有结构变化，无配置/文件格式影响。回滚即还原 `IdtState.cpp` 与 spec 改动。

## Spec 同步（Phase 3）

更新 `.trellis/spec/native-desktop/whiteboard.md` 第 32、83 行附近关于 250ms 退避与“同一可重试失败预算”的表述为“按类别 15 秒无进展预算、类别进展清零、退避 250ms 起上限 2s”。

# 原生窗口排序回归：20261021a → 当前

## 范围与证据

- 发布基线：`8e25c02ddff4928d58dd16f0484725fae12ff954`，2026-10-21 07:36，`Prepare to publish 20261021a`。
- 明确转折提交：`77f76038d28f64f2df1d2f18853cf20dd2ffefff`，2026-10-24 13:43，`fix(canvas): harden recovery gates and window command lifecycle`。
- 通过 `git log -S`、`git blame HEAD` 和 `git show 8e25c02d / 77f76038^ / 77f76038` 交叉核对；基线确为该提交祖先。不是仅按时间或提交标题归因。
- 本轮仅历史只读分析；未运行 GUI、进程探测、编译或测试。仅新建本报告。

## 结论

`77f76038` 同时改变了白板的非置顶排序算法、把完整排序作为模式提交必要条件，并新增将层级故障扩展成画布重建的恢复屏障。这是相对 20261021a 可明确定位的回归来源。

此前已经有 `HWND_TOP`、Bar 应在 PPT 上方、白板 NOTOPMOST 和激活样式切换；不能把这些概念本身误称此次新引入。新增的问题是：对隐藏/非活动 PPT 也执行完整组全局置前，再要求完整结果成功；失败后反复重建无关画布状态。

## Before / After

| 路径 | 20261021a（8e25c02d） | 77f76038 起 |
| --- | --- | --- |
| 白板 mode 命令 | `Window.cpp:1973–1974` 仅 `ApplyWhiteboardWindowMode(...)` | `77f:2575–2576` 改为 `ApplyWhiteboardWindowMode(...) && RefreshOverlayGroup(&command)` |
| 非置顶 Refresh | 基线 `2252–2375` 中只有 `keepTopmost` 才采完整 PPT 次序并逐成员 TOP；白板分支 `2359–2369` 只尝试 Bar TOP；最终只验证根的 topmost 状态 | 新 helper 两种 band 均采九成员；对基础链、四个 PPT（包含隐藏 Middle）、最后 Bar 依次 TOP；随后检查 owner、所有 topmost 位、全局连续性、Bar/PPT 和 PPT 内部次序 |
| 实际显示/交互某个 PPT | 基线 `2033–2050 / 2377–2397` 先 Bar TOP，再把当前 PPT 插到 Bar 后 | 改为 `RefreshOverlayGroup(&command, hwnd)`，一次单窗 Show/Promote 重排整个组，包括原本隐藏的 PPT |
| 普通窗口命令失败 | 基线为 promise 回执，无 `CommandSnapshot`、`ExecuteTracked`、`commandRecovery_` 持续恢复屏障 | 新增快照恢复；`snapshot.group` 包含 Refresh/Promote/Topmost/Fullscreen，且 `snapshot.canvas = snapshot.group || ...` |
| 纯排序失败恢复 | 无统一强制画布重建 | 强制 `canvasPolicyDirty_=true`、`EnsureCanvasPolicy(snapshot.mode, true)`，再执行同一个 `RefreshOverlayGroup()`；恢复不完就保留屏障并反复重试 |

上述完整排序 helper、mode 后置完整排序、快照/恢复域扩张均首次来自 `77f76038`。`Window.ZOrderPolicy.h` 也由该提交新增。其父版本仍保留基线的非置顶独立分支。

## 与已采现场对应

1. 初次点击时主状态在 Inactive/Entering 间反复，背景尚未请求；这与新增 mode 完整排序门在背景启动之前失败对应。StateMonitoring 的入口面板收起代码基线已有（历史来自 `2e21bf87`）；新增失败路径频繁命中它，产生 More 展开后立即被收起。
2. 九成员连续、owner 正确、全部非置顶，但隐藏 Middle 在 Bar 上方，违反独立的 BarAbovePpt 条件。不能把本次已观测现场归因成外部窗口夹入；仅删除 continuous 检查也不能修复该现场。
3. 用户通过任务栏进入后，phase=Active、Whiteboard workspace、背景 active/committed 均稳定，但 Recovery 连续 20/20 engaged，Primary visible 翻转。它与新增恢复链一致：排序/事务失败 → 恢复整个组 → 强制 CanvasPolicy → Hide 两画布和重做 Freeze/Drawpad 激活样式 → 恢复再碰排序失败 → 不清屏障。
4. 当前 `ApplyWhiteboardActivationStyle` 即使实际 desired style 已相等，也仍会 Hide/FRAMECHANGED/Show。这个 helper 的隐藏流程基线已有；`77f76038` 新增恢复机制才把它用于持续的纯层级故障恢复。应区分原有必要模式切换与本次错误重复调用。
5. 单次 TopWindow refresh failed 也可能只是被旧 Recovery 拒绝，不证明最早失败命令恰为 Refresh。现场尚未直接读取每次 Bar TOP 的 BOOL、最终 `after` 栈局部及 Recovery 捕获的最初命令类型。因此可明确归因到引入问题的控制流提交，但不能声称已捕获 Windows 内部拒绝排序的具体原因。

## 最小且可确信的修复边界

- 保留 Desktop/PPT 原置顶与安全清理；不整体回滚 `77f76038`，它还包括其他恢复、准入和命令生命周期保护。
- 白板分支恢复独立的非置顶处理：只要求有效句柄/代际、正确 owner、实际 NOTOPMOST、基础链相对顺序，以及 Bar/PPT 内部顺序；全局连续性不参与白板进入/呈现准入。
- 不再将所有隐藏 PPT 依次提升到全局 TOP。Bar 保留当前位置，对位于期望前驱上方的 PPT 仅下调到前驱后方，维护目标子序列；使用 NOACTIVATE 与 NOOWNERZORDER，末尾真实读回核验。需要 promoted 时通过下调挡在目标上面的其他 PPT 实现。实际 Win32 失败/内部关系失配仍不能当成成功。
- 纯 Refresh/Promote/Topmost/Fullscreen 的恢复只涉及它实际修改的 owner/band/相对序；不得无条件触发画布策略、激活样式、全组几何和显隐恢复。真正模式/画布几何事务继续保留安全恢复责任与过期命令清理。
- 入口一次性 UI 收起必须绑定新意图/epoch，而不是每次窗口收敛重试；失败仍保持可重试且可取消的明确状态。
- 保留上一轮 Present 解锁、异步单次激活、epoch guard、真实首帧/背景及可见性读回；它们没有改变以上新增完整组硬门，故不能单独修复这条回归。
- 仅用离屏/无窗口测试验证本轮修改：生产排序策略故障注入应覆盖隐藏 Middle 高于 Bar、连续性正确但内部顺序错、后台相对排序、API/读回失败、纯 Z 故障不得触发画布 Hide/Show、stale intent 与恢复屏障。不得将这些测试写成新的 GUI 已通过结论。

## 可复核命令

```text
git log --oneline -S RefreshOverlayGroup --all -- Inkeys/Inkeys/Window/Window.cpp
git log --oneline -S "snapshot.canvas = snapshot.group" --all -- Inkeys/Inkeys/Window/Window.cpp
git diff 77f76038^ 77f76038 -- Inkeys/Inkeys/Window/Window.cpp
git show 8e25c02d:Inkeys/Inkeys/Window/Window.cpp
git log --oneline --all --follow -- Inkeys/Inkeys/Window/Window.ZOrderPolicy.h
```

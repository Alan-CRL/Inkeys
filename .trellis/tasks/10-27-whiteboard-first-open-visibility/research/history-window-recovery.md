# Window Recovery 历史归因与最小修复边界

## 范围与证据等级

本报告只调查 `20261021a`（`8e25c02d`）到当前 HEAD 的命令恢复历史，不重复原生 Z 序算法的历史调查，不修改产品、已有文档或 shader。现场字段来自主会话已经提供的快照，本子代理本阶段未读取运行中进程、执行 GUI、编译或测试。

- 历史提交归属、旧/新调用关系：由 Git 源码直接确认。
- 现场首开与进入后闪烁的状态：引用主会话观测，并与源码传播链对应。
- 修改方案与需验证合同：设计建议，尚非已验证的产品修复。

## 1. 确切引入提交

`77f76038d28f64f2df1d2f18853cf20dd2ffefff`

- 时间：2026-10-24 13:43:16 +0800。
- 标题：`fix(canvas): harden recovery gates and window command lifecycle`。
- 直接父提交：`89d1237d2a9cf54b0a8454e85c3a040c557a3cef`，2026-10-24 09:41:09 +0800，`fix(canvas): restore input after failed scene handoffs`。

以下机制均由 `77f76038` 一次引入，当前相关行的 blame 也仍指向它：

| 新机制 | 在该提交 Window.cpp 中的位置 |
| --- | --- |
| `ExecuteTracked` 包装执行 | 2483 起 |
| `CommandSnapshot`、`CaptureCommandSnapshot`、`Recovery` | 2246、2276、2270 起 |
| `snapshot.canvas = snapshot.group || ...` | 2289 |
| `RestoreCommandSnapshot` | 2317 起 |
| 恢复时逐窗 FRAMECHANGED | 2352 |
| 恢复时强制 `EnsureCanvasPolicy(snapshot.mode, true)` | 2354–2357 |
| 恢复时重新完整 `RefreshOverlayGroup()` | 2359 |
| 恢复时逐窗 `ShowWindow` | 2366 |
| `RetryCommandRecovery` | 2377 起 |
| 普通命令先经过共享恢复屏障 | 2502 |
| 失败保存恢复责任并立即重试 | 2544–2545 |
| owner 消息循环恢复责任存在时使用250ms等待 | 1031 起 |
| Enter 从仅样式提交变为样式且完整组刷新成功 | 2576 |

上述行号以 `git show 77f76038:Inkeys/Inkeys/Window/Window.cpp` 为准，避免与当前工作树后续插入行混淆。

复核命令包括：

```text
git log -S CaptureCommandSnapshot 8e25c02d..HEAD -- Inkeys/Inkeys/Window/Window.cpp
git log -S "snapshot.canvas = snapshot.group" 8e25c02d..HEAD -- Inkeys/Inkeys/Window/Window.cpp
git log -S "EnsureCanvasPolicy(snapshot.mode, true)" 8e25c02d..HEAD -- Inkeys/Inkeys/Window/Window.cpp
git blame -L 2467,2472 -- Inkeys/Inkeys/Window/Window.cpp
```

不能因此整体回退该提交。它同时引入有价值的超时回执、失效保护、物理副作用撤回及后到安全隐藏/穿透合同；应修正恢复写集过宽和准入/排序耦合。

## 2. 旧版与新版失败语义的确定差异

### 20261021a 及77的父提交

- `8e25c02d` 的 `DrainCommands` 在 Window.cpp:1482 起，直接执行 `Execute(command)`，再向该命令的 promise 写入 bool。
- `89d1237d` 对应位置为1503起，仍是同一模型。
- Z 刷新失败只影响该次返回结果，没有通用 CommandSnapshot、整组窗口恢复或共享 commandRecovery 屏障。
- `8e25c02d` 的白板模式命令在1974直接返回 `ApplyWhiteboardWindowMode(mode)`；`89d1237d` 对应1995。模式准入没有附带完整 `RefreshOverlayGroup` 的成功条件。
- CanvasPolicy 当时已有自己的成对隐藏/策略提交/回滚，但不会仅因为普通 Z 刷新返回 false 就由通用恢复强制重跑。

因此，即使旧版有层级不符合预期，源码中也没有现在这条“Z 刷新失败→通用整组 Canvas 恢复→所有后续普通命令被恢复屏障挡住”的放大链。这不是声称旧版任何情况下都不可能闪烁，而是本次特定回归机制在旧版不存在。

### 77f76038以后

1. 模式命令执行样式成功后，还须 `RefreshOverlayGroup(&command)` 成功。
2. 任一普通命令失败、过期或 guard 失效，都可能进入其预先保存的通用恢复闭包。
3. Refresh/Promote/Topmost/Fullscreen 都属于 `snapshot.group`；`snapshot.canvas = snapshot.group` 又把它们全部升级成 Canvas 恢复。
4. `RestoreCommandSnapshot` 对所有快照窗口执行几何/FRAMECHANGED，再重跑 CanvasPolicy 和完整组刷新，最后还尝试恢复显隐。
5. CanvasPolicy 会先隐藏两块画布，并在 `changeStyle=true` 时隐藏 Freeze/Drawpad、重写激活样式及 FRAMECHANGED，再恢复显隐。
6. 若同一内部排序或其他后置条件持续失败，恢复闭包不退休；消息循环和后续命令会继续触发恢复。
7. 普通命令在执行前须先通过同一 overlay recovery。于是即使 Draw3/背景已就绪，Show、Bounds、模式及后端样式命令也可能持续被拒绝。

关键问题是恢复写入了原命令根本没有写过的 Canvas/geometry/visibility 状态，并让 Z 条件失败继续作为整组恢复的必要条件。

## 3. 对本次两阶段现场的解释边界

### 首次未点任务栏

主会话提供：phase 在 Inactive/Entering 间、workspace=Desktop、背景 active=false；Recovery 仅3/20次存在。

这说明不能将首开全部归因于一个始终不退的恢复屏障。源码足以解释：初始 Enter 的样式/完整组刷新未一起成功，StateMonitoring 回到 Inactive，下次同意图重做准备并关闭 More。恢复有时能够完成，但下一次 Enter 仍可能被相同准入条件拒绝，Draw3 Whiteboard 和背景的后续步骤尚未发生。

### 点任务栏进入后，切回其他应用闪烁

主会话提供：phase=Active、workspace=Whiteboard、背景 active/committed=true、mode=true均稳定；Recovery 20/20次存在；Primary 可见性反复变化。日志明确记录 TopWindow 刷新失败，owner正确、全部非置顶、9成员连续、foreignGaps=0；隐藏 MiddleLeft/Right 排在 Bar 上方。

当前 `GroupMatches` 无条件要求 Bar 高于四个 PPT，包括隐藏 Middle。因此现场已经有明确的内部失败条件，不能继续仅用“跨应用 continuous 太强”作为解释或修复。

在这个现场，普通周期 Refresh 的失败可沿第2节的宽写集恢复链反复隐藏/重呈现 Canvas，且恢复仍被原内部条件否决。这与持续闪烁、Recovery长期存在、Draw3和背景ready不变相符；设置Present锁和激活失败不是这组稳定现场所证明的阻塞点。

仍应由主会话的原生排序调查确定为什么现有重排未修正隐藏PPT相对Bar的位置；本报告不替代该结论。

## 4. Z-only恢复的最小实现边界

### 命令分类及控制流

- Z-only：`RefreshTopmost`、`PromotePpt`、`SetOverlayTopmost`、`SetOverlayFullscreen`。
- 完整恢复继续覆盖：模式切换、最小化/恢复、真实显隐/画布几何/CanvasPolicy事务。
- `RestoreCommandSnapshot` 必须在公共逐窗 FRAMECHANGED/Show/CanvasPolicy 之前，显式分流到 Z-only恢复。
- 不能仅设置 `snapshot.canvas=false`：公共路径仍会 FRAMECHANGED 和 Show，依然越过原写集。

### Z-only快照所需字段

| 字段 | 作用 |
| --- | --- |
| 命令类别/写集标记 | 确保只走Z恢复，不误触完整窗口恢复 |
| 每个参与角色的HWND与generation | 拒绝旧一代写回新窗口 |
| 每个受管窗口的owner、band及角色 | 读回并修复真实受管关系，正确项避免重复写入 |
| mode上下文 | 判断有效band与内部合同；Z命令不取得改写mode的权限 |
| 请求前四个PPT的相对序/排列及数量 | Promote失败后能恢复兄弟关系，而非把变化后的当前序假称原序 |
| 该命令实际改写的旧topmost/fullscreen政策 | 仅对应Set命令回写各自政策；Refresh/Promote不改无关政策 |

可复用既有固定 GroupSnapshot/成员数组并补充代际，避免动态注册表。Z-only不需要写回原bounds、visible、iconic或完整非TOPMOST exstyle；即使继续捕获用于诊断，也不得恢复这些无关字段。

### 恢复动作与成功条件

- 仅在真实owner/band不符时修正；排序使用受管内部合同，不能依赖获得其他应用的前台权限。
- 保持Bar位置，仅下调PPT并核验实际内部关系；隐藏PPT在本次既有严格合同中仍须真实修正，不能忽略隐藏项或改一个bool假通过。
- PPT兄弟前序的恢复仍要满足Bar高于PPT的安全合同，不能刻意恢复一个已知非法的跨组前态。
- 真实owner/band/内部序仍失败，继续保留恢复责任和明确stage；禁止超时清空或虚构成功。
- 成功后退休失败命令的回执，保留Rejected/Expired等真实结果，再解除恢复屏障；不能把失败的Promote冒充成功。
- 回滚本身不能套原命令已过期的Current guard；应按窗口代际、生命周期和更新的安全覆盖判断写回权限。
- Shell最终发布最新绝对状态，不复活过期TRUE；COM继续只在既有独立worker执行。

## 5. 必须保留的安全合同

1. **后到Hide/穿透优先**：恢复期间的Hide、HideAll、Hidden surface和开启穿透仍可进入安全清理路径。旧快照不得重新显示或关闭新穿透；AllowShow/AllowInput只在新请求真实提交后清对应代际保护。
2. **代际**：所有写回核验角色HWND与generation。旧目标退休不意味着替新窗口确认ready，不能把同值重用的HWND视为旧生命周期。
3. **stop/shutdown**：保持正式退出的显示/激活拒绝和隐藏收束；Z恢复不得Show、激活或恢复退出前的可见内容。
4. **回执与期限**：有限等待不等于物理操作未发生；在途失败须真实撤回。回滚健康后才能退休，不能因为1500ms到期就忽略副作用。
5. **内部顺序**：owner链、有效band、Bar高于PPT、PPT兄弟顺序均按真实结果核验；单一APPWINDOW/NOTOPMOST及一次显式激活合同保持。
6. **实际写集**：Z修复不能重置CanvasPolicy、region、NonRude、后端LAYERED/NOREDIRECTIONBITMAP、几何或可见性。
7. **原生所有权**：所有这些窗口写入仍由所属owner执行；不新增跨线程SetWindowPos/样式写者，不用AttachThreadInput或无限抢焦点掩盖失败。

## 6. 需要锁定的无窗口/离屏回归

- 生产Z-only恢复helper多次失败/重试时，Hide/Show/FRAMECHANGED/CanvasPolicy及无关style/geometry操作计数全为0。
- 同型的隐藏Middle在Bar上方必须通过真实策略生成下调操作并在成功前读回内部顺序；错误owner、混合band和旧代际仍不通过。
- Promote失败/过期后恢复请求前PPT兄弟序，且Bar>PPT保持。
- 后到Hide/穿透、新代际及shutdown在Z恢复中不被撤销。
- 健康的物理Z恢复能退休旧失败；真实native失败继续保留责任，原命令过期不阻止其必要恢复。
- 完整模式/Canvas事务失败仍保留原有成对隐藏、严格回滚和输入安全恢复；不能因Z分流而把所有恢复都降成空操作。
- 新minimized Freeze无写入拒绝不反而触发通用恢复；成对Bounds在后续policy失败/过期时两面都还原。

本阶段没有执行这些测试。后续验证应遵守用户新的“禁止GUI，仅离屏/无窗口”限制，不再读取运行时现场来替代测试。

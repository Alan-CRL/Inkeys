# Research: 白板主回归的生产 helper 与无窗口验证设计

- Query: 在禁止后续 GUI/进程探测的条件下，为后台白板排序、纯 Z 恢复和反复收起主栏提供真实生产复用的最小实现及故障注入方案；区分本轮回归与前文最小化/背景 token 扩展。
- Scope: internal；只读产品源码、任务文档及已经保存的现场文件。
- Date: 2026-10-27
- Active task: `.trellis/tasks/10-27-whiteboard-first-open-visibility`。

## Findings

### 1. 已读取的文件与可复用入口

| 文件 | 用途及代码位置 |
| --- | --- |
| `Inkeys/Inkeys/Window/Window.ZOrderPolicy.h` | 现有固定数组 `ZOrderSnapshot` 和 `Above/Matches`，适合容纳小型纯排序策略；不必新建公共模块。`Matches` 当前把 `continuous` 作为所有模式的硬门，见 28–41 行。 |
| `Inkeys/Inkeys/Window/Window.cpp` | `CaptureGroupZOrder:1815`、`ExpectedGroupOwners:1881`、`GroupMatches:1895`、`RefreshOverlayGroup:1903`；真实 HWND owner、generation、NOTOPMOST 及排序接线。 |
| `Inkeys/Inkeys/Window/Window.cpp` | `CaptureCommandSnapshot:2458`、`RestoreCommandSnapshot:2499`、`RetryCommandRecovery:2559`、`ExecuteTracked:2665`；纯排序被纳入完整画布恢复的实际入口。 |
| `Inkeys/Inkeys/Window/Window.CanvasPolicy.h` | `ApplyCanvasPairTransaction:172` 已采用生产调用小 Ops 模板、测试使用 FakeOps 的模式；不需要引入新的测试框架或公开 Service 内部状态。 |
| `Inkeys/Inkeys/Window/Window.CommandCompletion.h` | 真实回执状态机；`Begin/Current/Commit/Retired` 保留在途责任、失败与超时语义。 |
| `InkeysHeadlessTests/window_tests.cpp` | `RunWindowCommandCompletionTests:595`；现有 4 成员快照校验在 657 附近，只有谓词断言，尚未调用真实排序算法。 |
| `InkeysHeadlessTests/animation_tests.cpp` | `1792` 无条件运行上述纯测试，`1794` 才根据 `runWindowTests` 运行实际 HWND 测试；`--no-window` 的参数处理见 `1731`。 |
| `Inkeys/IdtState.cpp` | 同一失败意图重复调用 `CollapseAuxiliaryPanels`，见 `2562–2575`、`2723–2725`。 |
| `Inkeys/Inkeys/UI/Whiteboard/Whiteboard.cpp` | 背景 active/committed 与最小化退出为另一组生命周期问题；`RenderBackground:139`、`BackgroundMatchesActive:397`。 |
| 本任务 `design.md`、`implement.md`、`research/history-native-order.md`、`research/history-window-recovery.md` | 当前实施计划、历史归因和恢复边界。部分授权/调查叙述早于最新用户禁令，不能作为继续 GUI 的许可。 |
| `Build/WhiteboardGUIInvestigation20261027/whiteboard-failure-state-1.json` 等 | 已保存的旧现场；本轮仅读取文件，没有运行采集器、附加进程或查询窗口。 |

行号为本研究开始时工作树的位置；其他执行者后续修改会使行号移动。

### 2. 离屏夹具必须复现的证据

保存的首次失败 JSON 中，上到下顺序为：

```text
MiddleRight, MiddleLeft, BottomRight, BottomLeft, Bar,
Drawpad, DrawpadPresentation, Freeze, MagnifierHost
```

全部九成员实际 owner 正确、均非 TOPMOST、没有外部窗口夹入；四个 PPT 当时隐藏，Bar 可见，Freeze 不可见且未最小化。因此必须以 `continuous=true` 的以上序列构造首要回归，不能只用 `continuous=false` 证明放宽谓词。

文件聚合再次确认：`whiteboard-runtime-series.jsonl` 的 20 个样本为 Inactive 18 / Entering 2，Recovery engaged 3；任务栏操作之后 `whiteboard-after-taskbar-runtime-series.jsonl` 的 20 个样本全部 Active、Recovery 全部 engaged。`whiteboard-raised-runtime-series.jsonl` 的已保存样本则 Active、Recovery disengaged。

该证据支持分别修复“进入前严格排序不收敛”和“进入后层级失败触发画布恢复”；它没有捕获最早一条 SetWindowPos 的返回值，不能把某个具体 USER32 拒绝原因写成已经实测。

### 3. 排序 helper 的最小生产边界

建议沿用现有 `Window.ZOrderPolicy.h`，只增加这两类能力：

1. 固定快照的内部合法性校验：`continuous` 保持采集、保留 Desktop/PPT 原来的严格默认语义；Whiteboard 显式选择只验证内部合同。可以为现有 `Matches` 增加默认仍为 true 的 continuity 参数，也可以增加小型 `MatchesInternal`，避免修改旧调用的默认行为。
2. 一个生产真正调用的 Ops 模板：固定九成员/四个 PPT 的相对顺序收敛。窗口句柄、owner、generation、ordinal、topmost 来自生产真实采样，测试替换的只是操作环境。

不建立通用窗口事务框架、动态角色注册、第二套窗口模型或公共测试 API。现有 `RefreshOverlayGroup` 的 Whiteboard 分支必须实际调用该 helper；只给测试新增一个独立算法没有意义。

建议的窄接口如下（名称可按项目习惯调整，这里是实施设计而非要求新增这些公开符号）：

```cpp
// Identity 是本次开始时的固定 HWND + generation，PPT 次序是固定四项排列。
// Ops 的当前性检查：正向使用原命令 current；恢复使用物理生命周期检查。
template<std::size_t Count, class Ops>
bool ReconcileWhiteboardZOrder(const ZOrderSnapshot<Count>& identity,
    const std::array<HWND, Count>& expectedOwners,
    const std::array<std::size_t, 4>& pptOrder,
    std::size_t pptCount, Ops& ops);
```

`Ops` 只映射已有 Window Service owner 上的实际操作：读取快照、检查身份/意图、修复 owner、根降到 NOTOPMOST、必要的残余 band 修复、将 PPT 放在指定兄弟之后。辅助方法可以保留在 Window.cpp 的局部 Ops 中，但**算法决定顺序、是否需要移动及最终是否成功**必须在生产共享 helper 中。

若为保持 helper 更小而将既有 owner/band 修复留在 Window.cpp，则 helper 必须在移动前及最终读回明确拒绝仍错误的 owner/band；测试报告也应明确这是“排序及准入策略动态测试”，不是所有 owner 修复 API 的动态覆盖。

具体步骤：

1. 捕获参与窗口的固定 HWND 和 generation。当前 `CaptureGroupZOrder` 只在单次采样内验证 generation，随后 `RefreshOverlayGroup` 的写入只比较 HWND/IsWindow；应让 retained identity 跨整次操作有效。可以在现有固定快照里保留 generation 数组。Setting 的第十可选槽不参与本次写入。
2. 从开始时的真实 ordinal 稳定排列四个现存 PPT。promoted 存在时移到 PPT 子序列首位，其余项保持开始顺序。拒绝不属于本次已捕获 PPT 的 promoted；不因目标不认识而静默改成普通刷新。
3. 每轮检查命令/生命周期、固定身份、实际 owner。沿用既有受管线程修复和真实读回；根只降到 NOTOPMOST，最终全部参与成员均须实际 NOTOPMOST。混合 band 未真实纠正不能通过。
4. Bar 存在时以 Bar 作 anchor。按期望 PPT 从上到下处理：仅当当前 PPT 在 anchor 上方时调用 `SetWindowPos(ppt, anchor, ..., SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER)` 下调；随后 anchor=ppt。绝不对健康的 Bar 或基础链调用全局 HWND_TOP。
5. **每次实际移动后刷新采样**，再判断下一项。不能把首次 snapshot 的 ordinal 用到所有移动之后。例如先把 MiddleRight 放到 Bar 后，旧采样会误判 MiddleLeft 已在 MiddleRight 下方，跳过必要移动。
6. 无 Bar 的已有部分 fixture：以期望 PPT 首项为 anchor，只下调其余 PPT。不得用 Drawpad/Freeze owner 作 anchor 把 owned 窗口放在 owner 下面。无 PPT 则只校验基础内部合同。
7. 每轮末尾重新采样，检查固定身份、正确 owner、实际 NOTOPMOST、基础 owned 顺序、Bar 高于**所有** PPT（隐藏 Middle 同样检查）和完整目标 PPT 序。只有全部成立才返回 true。
8. 整个白板收敛最多两轮，不能保留 Window.cpp 外层两轮再让模板内部另跑两轮。真实 API 失败、读回失败或过期立即失败；允许第一轮成功返回但顺序仍不收敛时第二轮重读修正。两轮之后仍不成立则保留失败。

这里“Bar 原位”指不主动写 Bar 的 Z 序、保留它相对外部窗口的位置；**不能断言 Bar 的绝对 ordinal 不变**，因为下调原在 Bar 上方的四个 PPT 本来就会改变其枚举 rank。异常 mixed-band 的必要 band 修复应单独计数，不能伪称 Bar 永远无任何修复写入。

### 4. 真实生产 helper 的无窗口排序矩阵

在 `RunWindowCommandCompletionTests` 增加一个很小的 FakeOps：固定句柄/owner/generation/band，外部窗口也进入一个 top→bottom 向量；`PlaceAfter` 只执行“移除目标、插入 anchor 后”这一底层操作语义。它不计算期望 PPT 排列或决定该移哪项；这些只能由生产 helper 完成。期望结果直接写为数组或表。

| 场景 | 必须断言 |
| --- | --- |
| 上述现场序列，continuous=true | 实际产生四次 PPT 下调；最终 Bar > MR > ML > BR > BL > Drawpad；无 Bar/基础链全局 TOP、无激活。 |
| 健康序列 | 不产生 PPT 移动；显隐、owner、band、geometry 都不变。 |
| 外部窗口穿插且本应用后台 | 白板内部合同可成功；外部窗相对 Bar 不变；没有跨应用全局置前请求。旧 Desktop/PPT 默认 `Matches` 仍拒绝不连续。 |
| 所有 24 个 PPT 初始排列 | 普通刷新保持初始 PPT 相对序，仅使其位于 Bar 下；不能只测试一种排列。 |
| 四个目标分别 promoted | promoted 在 PPT 首位，其余三项原相对序稳定；Bar 仍在其上。 |
| 隐藏/可见 Middle 混合 | 结果与可见性无关；不能通过忽略隐藏窗使谓词通过。 |
| 无 Bar / 无 Mag / 缺少部分 PPT 的既有有效 fixture | 有限完成或明确拒绝；保持 owned 基础链，绝不把 PPT 放到 owner 下方。 |
| 原生操作返回 false、Capture 失败、成功返回但实际未移动 | 不报告假成功；不发生无限重试。一次被忽略的移动可在第二轮修正；持续忽略最多两轮。 |
| owner 错误、mixed band 未修复、owned below owner | 必须拒绝；不是仅检查 topmost 根或一枚 Bar。 |
| 同 HWND generation 更新、HWND 替换/消失、同步重入取消 | 不向新代际写入；调用返回失败；末尾不能靠新快照自动接受新窗口。 |

按 `failAt` 遍历每个 Capture/owner-write/band-write/relative-move 故障位置；另设“返回成功但不应用”的故障，防止只测试 BOOL 路径。全局 HWND_TOP/激活在 FakeOps 中设为禁止动作或独立零计数。允许根的必要 HWND_NOTOPMOST，不把它与全局抬升混为一谈。

### 5. Z-only 恢复的实际分流与失败注入

当前 `CaptureCommandSnapshot:2467–2472` 令纯层级命令也 `group=true, canvas=true`。恢复公共路径总是 `FRAMECHANGED:2532–2534`，之后 `EnsureCanvasPolicy:2539`、组刷新 `2541`、`ShowWindow:2548`。因此**仅把 canvas=false 不足以修复闪烁**。

最小 Z-only 分类：`RefreshTopmost`、`PromotePpt`、`SetOverlayTopmost`、`SetOverlayFullscreen`。`SetWhiteboardWindowMode`、Minimize/Restore、实际 surface visibility/Bounds 仍走原完整恢复。

在 `RestoreCommandSnapshot` 公共逐窗样式/geometry 恢复之前早分支到生产共享小 Ops helper：

- 固定成员 HWND/generation、正确 owner/band、请求前四个 PPT 兄弟顺序为物理恢复目标；不会恢复已知非法的 Bar<PPT 前态。
- Refresh/Promote 不写 mode/minimized/visibleBeforeMinimize；SetTopmost 只恢复旧 topmost 政策，SetFullscreen 只恢复旧 fullscreen 政策。
- 只修 owner、band、PPT 相对序并读回；白板调用上述同一排序 helper，桌面/PPT 继续原策略。
- 不访问或恢复旧 visible/iconic/RECT/region/非 TOPMOST exstyle；不调用 CanvasPolicy、activation style、Hide、Show 或 FRAMECHANGED。
- 原命令过期不是免除物理恢复的理由。恢复 guard 使用被捕获的窗口代际、生命周期和后到安全覆盖，不能复用已经 false 的原命令 Current。
- 后到 Hide/Hidden surface/开启穿透、shutdown 不得被旧快照撤销。Z-only 没有 Show/样式写入即天然避免大部分覆盖，但仍保留现有单调安全入口及代际校验。
- 真正 owner/band/内部序仍不符则保留恢复责任；物理恢复后由既有 `RetryCommandRecovery:2566–2572` 将原失败回执提交 false 或 Retired，绝不能 Commit(true)。

建议与生产相同的恢复 helper 使用 FakeOps spy，额外给“完整 native 恢复”入口记数。每种 Z-only 命令做以下序列：

```text
Begin 原命令 → 部分 Z 写入 → 注入失败 → 恢复失败多次
→ 保持责任和 Executing → 移除故障 → 物理恢复成功
→ 当前原命令 Rejected，或已到期原命令 Expired
```

整个序列中 `Hide==0, Show==0, FRAMECHANGED==0, EnsureCanvasPolicy==0`，并检查 activation-style/geometry/region/非 band style 写入全为 0。还要：

1. Promote 部分执行后失败/过期，恢复请求前 PPT 兄弟序；不能从失败后的当前序重新采样并假称已回滚。
2. 注入 owner 修复不落地、残留 TOPMOST、代际变化和采样失败，恢复继续 false；健康后才能解除责任。
3. 在原命令期限已过后实际修复成功，原命令仍 Expired 而非被救活为 Committed。
4. 在恢复期间注入后到 Hide/穿透和 shutdown，验证旧恢复没有显示或改回输入样式。
5. 以真正 mode/Canvas 事务作对照，仍调用原完整恢复并保留成对隐藏和 CanvasPolicy；不能把所有恢复都降为空操作。

这些测试必须调用真正被 `RestoreCommandSnapshot` 调用的 helper，不能在测试中重新实现完整恢复算法。命令到 Z-only 类别的映射和早分支位置还需要源码接线审查：只测试被调 helper，**不能**声称已经动态覆盖 Service 的所有私有 dispatch。若为动态覆盖接线而抽取小型 scope 选择 helper，应由生产恢复入口直接使用，仍保留在现有私有策略头内，不新增公共事务架构。

### 6. 反复关闭 More 的最小验证

本轮主回归的第三部分是 `IdtState.cpp:2567,2723` 的入口准备按每次失败重复执行。用现有 state-mode 生产测试入口或一个真实生产复用的私有小策略，保存 `lastPreparedEntryEpoch` 和 `nextWindowModeRetryAt` 即可；不必重做 workspace 状态机。

- 同一奇数 epoch 十次失败：Collapse/Cancel 计数为 1，不清 desired、不假发布 Active、不伪造 Draw3/background ready。
- 虚拟时间不到 250ms：额外唤醒不重新进入原生事务，且应在写 Entering 之前判断；到期可重试。
- 新 epoch、取消、真正退出立即处理，不受旧期限阻挡。
- 真正 Active→Exiting 的一次清理保留；新白板意图允许再次准备。

这里不能以重复读取源码字符串代替生产策略调用；测试也不需要创建 Bar HWND。

### 7. 本轮必要范围与应拆开的扩展

**本轮必要**：白板独立非置顶内部排序及真实读回、Z-only 恢复分流、同 epoch UI 准备去重/有界重试，以及对应生产 helper 无窗口测试。它们分别对应已保存的首次模式事务不收敛、Active 时恢复屏障/画布闪烁、More 立即关闭。

**保留已有修复，不撤回**：Settings Present 解锁、一次异步激活及 epoch guard、Shell 双表面与最新绝对状态、真实首帧/背景/显隐读回、用户 shader 文件。

**不作为主回归补丁的必要扩展**：minimized inactive 处理顺序、单调背景 token、Show/Hide/SetBounds 新公共 guard、成对 Bounds 扩大快照、完整窗口测试 fixture 的局部/全局 Service 修理。它们确有前文静态问题或覆盖缺口，但首次保存现场明确未最小化，Active 闪烁时也未最小化；当前两条现场不能证明这些是本轮症状根因。为修主回归而同时改 token/公共 API 会扩大并发与生命周期风险，宜另轮独立最小修复和回归。若主实现发现不改其中某项就无法维持当前物理恢复合同，再明确列出依赖及证据，而不是整包照搬旧 design D。

### 8. 相关 specs 与后续允许的验证

- `.trellis/workflow.md`：研究、实施、验证按任务保存；本报告不修改实施状态。
- `.trellis/spec/native-desktop/rendering-and-ui.md:1746–1755`：所属 HWND 线程写入、owner 链、Bar>PPT、根 band、白板 Shell 合同；`:1831–1843`：`--no-window` 与实机验证的界线。
- `.trellis/spec/native-desktop/whiteboard.md`：NOTOPMOST、单任务栏锚点、真实首帧/背景提交、Active 后一次异步激活；焦点失败允许后台进入。
- `.trellis/spec/native-desktop/index.md`：不能以 ARM64/headless 结果宣称其他 Windows 系统已经实测。
- `.trellis/spec/native/quality-and-validation.md`：最小改动、生产算法测试、保留编码/换行、明确未验证项。

后续执行者按已有构建规则使用 ARM64 原生 MSBuild、完整 `InkeysRepo.sln Debug|ARM64`，同 PowerShell invocation 先规范化重复 PATH，完整构建至少 5 分钟预算；然后执行 `InkeysHeadlessTests.exe --no-window` 及已有无 GUI state-mode 生产入口。当前研究者没有执行构建或测试。

## External References

本轮没有新增外部检索；沿用任务已有的官方 API 参考作为后续 native 行为核验入口，不把无窗口模型当作真实 USER32 证明：

- Microsoft Learn, `SetWindowPos`: <https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos>
- Microsoft Learn, Owned Windows: <https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#owned-windows>
- Microsoft Learn, `SetForegroundWindow`: <https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow>

## Caveats / Not Found

- 最新用户禁止后续 GUI；本轮只读源码及既存采集文件，没有启动 GUI、运行采集器、检查进程、编译或执行测试。
- 已保存现场没有最初失败 native stage 的 BOOL/即时错误码。可以锁定错误的严格排序/恢复控制流并测试其修复，不能声称已确认某条 USER32 调用具体被什么系统规则拒绝。
- 纯 Ops helper 可以证明生产决策、有限重试、输入身份和恢复写集；它不能证明 Win7/Win11 等真实系统的 owner 传播、Shell/taskbar 行为或视觉闪烁已经人工通过。必须在最终交付明确保留该验证边界。
- `RunWindowTests` 的 HWND fixture 不在本轮允许范围。不要为补测而启动它，亦不要用修好纯 fixture 替代实际产品 helper 的无窗口回归。

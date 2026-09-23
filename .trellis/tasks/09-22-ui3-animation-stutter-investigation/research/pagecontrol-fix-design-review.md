# Research: PageControl 光照无效唤醒的最小修复设计审查

- Query: 怎样在不新增公共 API 的条件下过滤无影响的共享光照通知；是否应同时在 PageControl 的 Render 前增加绘制门，且不漏动画、旧光擦除、失败和调试末帧？
- Scope: internal；仅当前源码、现有规格和测试工程的只读审查。
- Date: 2026-09-22
- 前置研究: [scheduler-recovery-audit.md](scheduler-recovery-audit.md)。本文件为待用户批准的设计建议，未实施产品或测试修改。

## Findings

### 建议结论

**第一批只修共享光照广播的通知选择，保留 PageControl 原有绘制/呈现入口和空 damage 兜底。** 可将产品改动局限于 `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp`，修改三个私有 helper 的返回值及一个调用点，不需要增加 module 公共 API、不需要重排动画推进、不需要改变 PageControl 生命周期。

过滤条件应表示“**本次应用光照时，确实向本 surface 贡献了需要重绘的像素范围**”。不能用 accumulated `pendingDamage` 是否为空、前后 RECT 是否不同或 `invalidated` 是否从 false 变 true 来代替这个信息。现有内部计算已经具备该信息，当前只是被 void 返回值丢弃。

第一批解决的是纯 cursor 变化对无相交分页窗的无用唤醒。主光/drawing color 变化继续沿用现有保守全脏策略；不把第一批扩大成“所有类型光照都精确裁剪”或通用渲染调度重构。

### 1. 三个现有私有 helper 足够，不必引入新状态或公共方法

代码位置均在 `Bar.Scene.cpp` 的 `BarSurfaceScene::Impl`：

| 现有方法 | 当前已掌握的信息 | 建议返回值 |
| --- | --- | --- |
| `IncludePresentationDamageLocked():605` | 将本次矩形裁剪到当前 presentation 后是否为空 | 裁剪非空并执行 union/invalidated 时返回 true；空时 false |
| `UpdateCursorLightDamageLocked():662` | 最新旧/新鼠标光实际贡献边界的 union，以及 cursor/bounds 是否变化 | 把本次 union 加入 damage 的结果；不因已有其它 pending damage 返回 true |
| `ApplySharedLightingLocked():734` | 本次 primary/mapping/cursor 是否变化 | `primaryChanged || cursorDamageContributed`；完全相同的 generation/mapping 则 false |

`IncludePresentationDamageLocked()` 目前只从 cursor damage 路径调用；`UpdateCursorLightDamageLocked()` 目前只从 `ApplySharedLightingLocked()` 调用。变为 bool 不会扩散到公共 module。

设计示意，非已实施补丁：

```cpp
bool IncludePresentationDamageLocked(const RECT& rect) noexcept
{
    const RECT damage = ClipToSurface(rect, PresentationLocalRect());
    if (IsEmpty(damage)) return false;
    UnionInPlace(pendingDamage, damage);
    invalidated = true;
    return true;
}

// UpdateCursorLightDamageLocked 保留现有 borders、resolved 和 boundsChanged 计算。
const bool contributes = (cursorLightChanged || boundsChanged)
    && IncludePresentationDamageLocked(resolved.damage);
cursorLightDamageBounds = resolved.current;
return contributes;

// ApplySharedLightingLocked 保留全部快照、generation 与 mapping 更新。
const bool cursorDamageContributed = cursorChanged
    && UpdateCursorLightDamageLocked(true);
if (primaryChanged) IncludeFullDamageLocked();
// ... 原有 appliedSharedLighting/generation/bounds/outset 赋值 ...
return primaryChanged || cursorDamageContributed;
```

注意不要写成 `primaryChanged || UpdateCursorLightDamageLocked(...)` 并利用短路逻辑跳过 cursor 更新：primary 和 cursor 同时变化时也要更新 cursor 历史边界，保证以后退场擦除。

`PublishSharedLighting():1893` 仍为每个 subscribed scene 应用最新快照，只在上述返回 true 时把 hooks 放入通知列表。共享 registry 锁、scene 锁和“释放锁后再调 hooks”顺序原样保留（`:1898` 到 `:1917`）。

其他调用点：

- `SetSharedLightingSubscribed():1878` 可以显式忽略返回值，随后仍执行现有全脏和唤醒。
- `Render():2328` 可以显式忽略返回值；它已经在本次真实回调中，且后面正在绘制，不需要在这里额外为“新光照”重复请求。
- `.cppm` 中 `PublishSharedLighting`、`SetSharedLightingSubscribed`、`Render` 签名不变。

### 2. 为什么不能只比较已有 flags 或累计 RECT

`pendingDamage` 是自上一次成功 `ConsumeDamage()` 后的**累计 union**；`invalidated` 也是累计状态（`:1971` 到 `:1976` 仅成功消费时清空）。两者都不记录“这次 Publish 的原因”。

| 简单做法 | 会出现的问题 |
| --- | --- |
| `if (scene.pendingDamage 非空) wake` | 旧业务/旧失败 damage 会被每次无影响光照重新当成光照需求；不满足本次过滤目的 |
| 比较调用前后 pendingDamage RECT | 同位置强度、颜色变化仍需要重绘，但 union 的 RECT 可以完全不变；已有大 damage 覆盖新小 damage 时也看不出变化 |
| 比较 invalidated 的 false→true | invalidated 已为 true 时，同区域新的有效光变化将被误判为无变化 |
| 只看当前 light influence 与 scene 相交 | 光从本 scene 移走时当前交集为空，但上次有光的像素仍须擦除 |
| 临时清空 pendingDamage 再应用光照 | 破坏旧业务与失败事务；还可能把未呈现的多个光照位置历史丢掉 |

因此应直接传播“本次计算出的贡献非空”这个已有局部判断，保留累计事务数据原样。

### 3. 各边界如何保持

| 场景 | 第一批修复后的行为与依据 |
| --- | --- |
| cursor 旧、新影响都在 scene 外 | 快照和 generation 更新；当前贡献为空；不通知该 scene |
| cursor 进入 scene | 当前实际边缘贡献非空，返回 true；按原路径绘制 |
| cursor 离开、隐藏或强度降到 0 | `ResolveBarSurfaceCursorLightDamage():193` 以 previous 为初值再 union current（`:206`）；旧贡献仍触发擦除，不能只测 current |
| 同矩形范围内强度/颜色变化 | `cursorChanged` 由完整属性比较得出（`:157`）；resolved union 非空，即使 accumulated RECT 不变也返回 true |
| primary 位置、半径、可见性、drawing color/blend/opacity 或 edge 开关变化 | `SameSharedPrimaryLighting():139` 保留现有比较；primaryChanged 继续全脏并通知，不尝试在此批做新的主光裁剪 |
| 首次订阅、再次订阅或退订 | `SetSharedLightingSubscribed():1868` 原有 `IncludeFullDamageLocked():1886` 与 hooks（`:1889`）保留；首次快照或关闭旧光不会被过滤 |
| scene 布局位置、缩放、margin 交接 | `SetBounds():1780` / `SetDamageOutsetDip():1847` 原有 invalidation 和 wake 保留；`ApplySharedLightingLocked():744` 的 mappingChanged 保留。PageControl 的 `ApplySceneBounds():697` 已跳过完全相同的 bounds 调用 |
| 新动画、hover、press、内容转换 | 原有 setter/pointer 路径自己 IncludeDamage 并 wake；动画已有 `Render():2387` 和 PageControl Continue 续帧，不依赖鼠标光广播维持 |
| 动画最后一帧 | 第一批不改变 `PresentScene`、`debugFrameSleepLatch` 或 `keepAnimating`；原最后一帧规则继续执行 |
| 前一次提交失败 | 第一批不清 pending、不取消请求位。`PageControl.cpp:1557` 返回 Retry，`RenderPipeline.cpp:193` 保留该 client；不需要无关光照代替失败恢复机制唤醒 |
| 多次 Publish 未成功呈现，之后光移走 | 每次 affected damage 继续累积，最后离开还 union 前一次范围；无影响的后续 Publish 不清旧 pending，所以成功呈现前的擦除责任保留 |
| 真正业务 invalidate 与无影响光变化同帧到达 | 业务自身已 wake，pending 保持；本次无影响光不再补一个理由。调度器按位合并仍会执行业务请求 |
| 调试开关、旧覆盖清理 | `PageControl.cpp:2170` 到 `:2180` 独立设置 overlay pending 并 RequestAll；不依赖光照广播 |

不建议在这个通知判断里再 OR 整个 scene 的 `animationActive` 或 `invalidated`。它们是其它调度原因，已各自拥有 wake/Continue/Retry；混入会使通知来源重新混淆。第一批只删除当前无效光照导致的额外通知，不取消任何已有请求或续帧。

### 4. PageControl 绘制门可以作为第二层，但必须与“是否继续处理回调”分开

`PageControl.cpp:1512` 当前只要可见就调用 `PresentScene()`。`PresentScene():955` 先 Clear，`:958` 才 Render，而动画在 `Bar.Scene.cpp:2333` 内推进。因此 `PendingDamage().empty()` 不是安全的完整跳帧条件。

现有公共 API 足以构建一个**保守门**，但这不是第一批必要改动。门应放在 ConfigureSurface、bounds 推进、订阅处理、目标尺寸和恢复判定之后，且只决定是否执行 D2D/ULW。以下所有原因都需保留进入现有 PresentScene 的能力：

- `forceFullReplacement`：首帧、presentation/backing 变化、epoch 变化、前次呈现失败。
- `scene.IsInvalidated()` 或非空 `scene.PendingDamage()`：包括刚启动尚未 Advance 的 hover/press/内容动画。
- `scene.AnimationActive()`：仍在活动的内部动画，即使这一时刻旧 pending 已被上次成功消费。
- `keepAnimating` 中的 host lifecycle：bounds、退出 deadline、长按 repeat；不能让光照优化停止过渡/输入门禁时钟。
- `debugOverlayRefreshPending`、待呈现的最终绿框，包括 latch 当前尚未进入 pending 但上轮活动已结束的情况。
- 资源缺失和不确定的显式刷新请求：缺乏经过审计的无操作判定时继续当前全量兜底。

保守做法可以**不提前 Advance**：以上只要任一原因成立，就沿用原有 Render 内 Advance；只有全部明确无工作时才跳过绘制。这避免为了建立 gate 而改两阶段动画时序。注意 `AnimationActive` 是上次 Advance 的结果，不能独用；PointerMove 在 `:2094`/`:2104` 会先标 dirty 并 wake，第一次 Render 才真正推进新动画，故必须同时检查 invalidated/damage。

#### 已有 Advance() 不是一个可随手插入的新准备阶段

`Bar.Scene.cppm:247` 已公开 `Advance(frameTime)`，实现 `Bar.Scene.cpp:2286`。它会推进状态、设置 damage，并在 active 时调用 hooks（`:2296`）；并非无副作用的状态查询。

同一 `frameTime` 下再调用 Render 会第二次运行 `AdvanceAnimationsLocked()`：dt 是 0（`:1022` 到 `:1025`），不会数值上再加完整 dt，但 `ApplyButtonTargetsLocked()`、内容转换与 damage 检查仍然重复。因此不能仅凭 dt=0 宣称完全幂等。若以后做“先 Advance 后决定是否 Render”，应单独验证零 dt 重入、结束帧以及 hooks 次数，而不是顺手引入双推进。

#### 跳过绘制不等于从 RenderSurface 提前返回 Idle

`PageControl.cpp:1560` 后还有呈现 revision 检查、成对拖动提交和 `SetBounds/Show/Hide`。这些属于窗口生命周期；`SetBounds/Show` 失败也必须 Retry（`:1594` 到 `:1606`），即使图像早已成功呈现且 scene damage 已被消费。

若加入绘制门：

- 未绘制时继续必要的窗口位置/显隐/拖动提交检查，不能直接从整个 callback 返回。
- 未做真实成功呈现时不得调用 `ConsumeDamage()`、推进 committed presentation tuple 或清 `forceFullPresentation`，也不得 `CommitPresented()` 调试 latch。需要一个局部“这次真的呈现成功”分支与窗口操作成功分支区分。
- `pendingDrag`、layout deadline、`windowCommitFailureActive` 等应保持续调度，不能被 scene 无 dirty 覆盖。
- 并发输入在门检查后写入的 scene damage 不能被跳过绘制分支清掉；保留请求位，让下一周期处理。

#### 调试最终帧不能只看 IsPending()

`DebugFrameSleepLatch::Update(enabled, active)` 在活动时会令 pending=false、presented=false。原代码直到 `PresentScene():969` 才根据本帧活动性把最后一帧标为 pending。若新的门仅检查已有 `IsPending()`，就可能在该 Update 执行之前跳过最终绿框。

第二层门必须保守保留 `showDebugFrames && !latch.IsPresented()` 的结算机会，或在门前以正确的活动性推进 latch；后者还与 Render 后才能精确知道的动画结束状态耦合。第一批不触碰此处可直接避免新增末帧漏刷风险。

### 5. 为什么第一批不同时做通用 gate

源头过滤已经切断“鼠标光变化→无相交分页窗收到请求→空 damage 全量回退”的确定路径。一般 gate 能过滤的更多类型请求涉及另一份合同：`.trellis/spec/native-desktop/rendering-and-ui.md:636` 明确要求未分类的非调试呈现请求回退全窗口。

当前 `RequestAll()` 的来源包含显示器变更（`PageControl.cpp:2077`）、业务快照（`:2136`/`:2150`）、调试（`:2170`）、布局通知（`:2183`）、拖动和取消交互。请求位没有保存“这是已确认无视觉影响的光照”这一原因。要把任何无 dirty 回调都当成 no-op，就必须先审计这些来源并明确首次资源、epoch、HWND 成功事实与末帧语义。

所以两种方案的范围可分开审阅：

| 方案 | 产品范围 | 风险/收益 |
| --- | --- | --- |
| 第一批：私有光照贡献 bool + 按贡献广播 | `Bar.Scene.cpp`；公共签名不变 | 切断已确认纯 cursor 无效通知；旧呈现事务完全沿用，回归面小 |
| 后续：PageControl 通用绘制门 | `PageControl.cpp`，可能需内部状态/调试规则配套 | 能过滤其它多余请求，但需完整审计动画、显隐、拖动、恢复和未分类请求 |

如果第一批后仍有显著无 damage 绘制，再用每客户端统计判断是否值得加入第二层。不能先为了“二次防线”扩大当前补丁，最后难以判断哪一项修复改变了表现。

### 6. 验证设计与现有测试边界

第一批关键用例：

| 输入 | 期待 |
| --- | --- |
| 两个稳定 scene，cursor 只在 A 附近移动 | A 通知；B 快照更新但通知数不增长 |
| cursor A→外部→外部 | 第一跳通知 A 擦旧光；后续在外部移动不继续通知 A |
| 同一影响 RECT，强度从 0.4→0.5 | 必须通知，不能依赖 damage RECT 扩大 |
| 已有完整 pending damage，cursor 在外部移动 | 旧 pending 不变；不把此次光变化计为有效通知 |
| 已有完整 pending damage，cursor 在内部变强度 | 此次应返回有贡献，尽管 union RECT 与原值相同 |
| primary/drawing color/edge 开关变化 | 延续现有全脏通知 |
| 首次订阅、退订、bounds/outset 修改 | 独立触发全脏通知 |
| 呈现失败后连续无影响 Publish | failure pending 与 Retry 保留，不提前消费；恢复后正确一次清除 |
| 动画活动到结束，期间 cursor 始终在其它窗口 | 动画靠原有续帧完整结束，调试最终帧仍呈现一次 |

**不要将这些写成已经通过的测试。** 本轮是设计只读调查，没有实施 bool 返回值修改，也没有运行修改后的场景。

当前 `InkeysHeadlessTests.vcxproj:95` 编译了 `Bar.Scene.cppm`，但没有登记 `Bar.Scene.cpp`；现有 `page_control_tests.cpp` / `whiteboard_ui_tests.cpp` 主要测公开纯函数。没有发现可直接创建真实 scene 并统计 Publish hooks 的现成测试。因此后续不可只写 `import Bar.Scene` 的实例测试就声称可链接，也不可为方便单测临时导出 Impl。

可接受的验证路线：优先复用当前已链接的光照边界纯函数做旧/新范围和 union 用例；通知集成用例应使用经过明确设计的内部可测接缝或无 GUI 的真实 scene 研究 harness。若为第一批最小补丁决定暂不引入复杂 scene harness，则明确交付静态调用链审查、相关纯函数回归及完整 solution 编译，并把 hooks/帧计数集成检查列为未执行项。不得用复写 `return primaryChanged || cursorDamage` 的测试冒充生产集成验证。

## Files Found

- `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp`：私有光照贡献计算、广播、累计 damage、场景动画和 hooks。
- `Inkeys/Inkeys/UI/Bar/Bar.Scene.cppm`：已有场景公开接口；包含 Advance/AnimationActive/IsInvalidated/PendingDamage，但不暴露 Impl。
- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp`：目标准备、绘制呈现、调试末帧、HWND 成功事务与各类请求源。
- `Inkeys/Inkeys/UI/PageControl/PageControl.cppm`：可见性、续帧、调试活动性等纯判定。
- `Inkeys/Inkeys/UI/Bar/Bar.FramePacing.cppm`：DebugFrameSleepLatch 的 pending/presented 状态。
- `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp`：已有 Continue/Retry 和按位请求保证。
- `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj`：Scene 仅接口在当前测试工程内的链接边界。

## External References

本专题只涉及仓库内部数据流，不引入新的第三方 API、版本或外部技术假设。相关 DXGI 参考见前置研究。

## Related Specs

- `.trellis/workflow.md`：停留规划，用户批准后方可实施。
- `.trellis/spec/native-desktop/rendering-and-ui.md:115`：各客户端自有 Continue/Retry，不依赖其他窗口唤醒。
- `.trellis/spec/native-desktop/rendering-and-ui.md:344` 附近的 PageControl 合同：复用共享 Bar、光照与输入/布局生命周期。
- `.trellis/spec/native-desktop/rendering-and-ui.md:632`：旧/新 dirty union、未分类需求兜底、提交成功才消费、调试最终帧。
- `.trellis/spec/native-desktop/rendering-and-ui.md:943`：设备、光源范围裁剪和缓存边界。
- `.trellis/spec/native-desktop/rendering-and-ui.md:1574`：HWND 显隐/位置提交失败必须 Retry，不能仅按 present 成功结束。

## Caveats / Not Found

- 第一批仅移除无影响的纯鼠标光广播。primaryChanged 当前仍全脏、真实相交光照仍绘制；没有宣称消除所有 PageControl 绘制成本。
- 该方案不修复主栏自身遮罩生成、capacity、dt 或 GetDC 的问题；它是独立可验证的小修复。
- 通用 gate 所需的无 GUI scene 集成测试入口当前未找到；不建议借此把本次补丁变成 Scene 测试架构重构。
- 当前设计依赖现有 producer 自己发送业务/动画请求、present 失败返回 Retry 的已读代码合同；如果后续实施发现其他已订阅客户端依赖“无关光照替它续帧”，应修复该客户端的调度责任，不能恢复错误的广播保活。

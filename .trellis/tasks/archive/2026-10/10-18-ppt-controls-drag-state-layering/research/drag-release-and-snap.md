# Research: PPT 控件拖动释放与中轴吸附

- Query: 调查控件抬手后微弱移动、两侧竖直控件中轴吸附缺失，找出底栏对应 DIP 阈值、生产位置提交路径和最小修复边界。
- Scope: mixed，内部生产源码为主；外部仅核对 Win32 Up 坐标和 Windows DIP 定义。
- Date: 2026-10-18
- Active task: .trellis/tasks/10-18-ppt-controls-drag-state-layering
- Baseline: 主会话提供 HEAD 13783fe7；先前 COM 修复 0eb1c7c5。本角色未运行 Git。
- Authorization: 本轮只研究与规划；未改生产代码、测试、项目配置或规范，未构建、运行测试、启动 HWND/GUI/Office。

## Findings

### 1. 结论和证据等级

| 项目 | 可直接确认的事实 | 对用户现象的解释强度 |
| --- | --- | --- |
| 抬手后继续移动 | render fallback 对一般位置变化继续使用 0.4 秒 bounds 缓动，Up 不结束这段缓动；drag commit 不检查实际几何是否已到候选 | 明确存在可形成余移的生产时序；尚无用户现场帧/日志，因此不是实机复现结论 |
| Up 最终点遗漏 | WM_LBUTTONUP 读取 local 仅供 PointerUp 命中；没有用 Up 的屏幕点再调用 UpdateDragLocked | 明确的最终采样缺口；自身只能丢掉最后一段位移，不能单独解释无后续输入的余移 |
| 侧栏中轴吸附缺失 | 生产 ClampPageControlLayout 仅裁剪范围；Move/runtime resolver 没有把小的 middlePairHeight 吸到 0 | 明确的行为缺口；当前不存在可调的侧栏 snap 阈值 |
| 中轴定义 | middlePairHeight=0 令主体中心对齐选定屏幕的水平中线 y=(top+bottom)/2 | 源码直接证明，不能误用屏幕竖直中线或 X 轴 |
| 阈值来源 | Bar 主栏中轴阈值为 40 DIP；底边阈值为 20 DIP；共享 PPT 底部翻页窗自身没有独立吸附逻辑 | 以“中轴”对应关系规划使用 40，20 不适用于本需求 |
| 归因于最近 COM 修复 | 主会话 Git 调查确认两个可能反馈版本的 PageControl 源文件相同，0eb1c7c5 未改拖动/吸附/锁路径 | 不能把当前拖动机制归因于最近 COM 改动；反馈实际版本仍未确认 |

本研究使用生产 Inkeys.UI.PageControl 链。Inkeys.UI.Ppt 接口单元中保留的 ResolveControlLayout / ClampPptDrag 主要由旧布局 helper 和 ppt_ui_tests 消费，不能只改这些 helper 后宣称生产拖动已修复。

### 2. 入口、线程和状态链

主要状态位于 Inkeys/Inkeys/UI/PageControl/PageControl.cpp:164（SurfaceState）和 :245-265（共享状态/锁）。

1. Window Service owner 的 WM_LBUTTONDOWN：
   - :2258 取得 renderTransactionMutex；
   - :2261-2273 通过实际 Scene 命中并捕获指针；
   - :2277-2291 为 DragHandle 立即拖动，Page/背景为 pending，保存 dragStartScreen、dragStartLayout、feasibleLayout，并 BeginDragTracking；
   - :2323-2326 在 Scene 事务锁外执行 PromotePptWindow / 业务命令。
2. WM_MOUSEMOVE：
   - :2182-2184 取屏幕坐标并进入 renderTransactionMutex；
   - :2194-2205 超过系统阈值后从 Page/background press 切到 drag，并撤销原 press；
   - :2207-2212 调用 UpdateDragLocked，解锁后 FlushCommittedDrag。
3. UpdateDragLocked：
   - :1381-1393 始终从原 dragStartLayout 加绝对屏幕位移，不按上一成功候选累加；左右 X 镜像，Y 均减 dy；
   - :1394-1416 取显示快照，核 session/epoch，运行适配、范围裁剪和 pair 碰撞判断；
   - :1418-1422 跳过与上一 feasible 相同的位置，发布 latest-wins 候选；
   - :1434-1450 用上一 feasible 和新候选分别解析绝对 logical bounds，加当前 Scene presentation outset，并确认 mode/size/scale 仅为平移；
   - :1452-1498 非纯平移、presentationMutex.try_lock 失败、窗口直移失败均 RequestDragPair，保留 mailbox pending；
   - :1500-1506 直移成功只更新两窗 bounds 并提交同一 pair revision，不修改 Scene。
4. 发布和提交：
   - :362-386 在 dragCommitMutex -> snapshotMutex 内写完 layout/mailbox，再 release-store directMoveRevision；
   - :427-445 两窗直移成功才一次提交完整 pair mask，并再发布 direct revision；
   - :1553-1579 在渲染/呈现锁外调用 commitPosition；
   - Inkeys/Inkeys/UI/Ppt/Ppt.cpp:136-144 只把同 session/epoch、单调 pair version 的已提交位置交给 PositionState，不写盘、不逐采样反向触发发布。
5. 取消：
   - PageControl.cpp:2423-2467 清 pointer/drag/touch；pending 明确回滚到最后 committed layout，再请求 pair；
   - ReleaseCapture 位于原事务锁外，以免同步 WM_CAPTURECHANGED 重入；这不代表其他锁链已无风险，完整卡死调查由并行研究负责。

### 3. 抬手后余移：最直接的生产机制

#### 3.1 正常直移成功，不应无条件归咎于残留惯性

PageControl.cpp:1254-1265 的 SetBoundsDirect 把当前值、start、target、scale 同时设为目标，active=false。:1500-1504 在两个 HWND 移动成功后对两面这样处理。正常释放随后 RequestAll，ConfigureSurface 以相同几何重建 Scene；若显示快照/预算/配置不变，RetargetBounds 在 :826-827 因 target 相同早退。

因此“所有直移都在松手时必然多滑一下”不是源码能够支持的结论。PageControl 没有沿用 Bar 底栏的弹簧/弹性惯性模型；不要为了止移全局关闭 Bar/Scene 的共享操作动画。

#### 3.2 fallback 把拖动位置当作普通布局动画

- PageControl.cpp:947-960 的 animateBounds 只看 bounds 是否初始化、前后可见和是否入场；没有区分用户拖动候选与普通布局改变。
- :809-845 的 RetargetBounds 以当前几何为 start，给新 target 启动缓动。
- :779-806 的 AdvanceBounds 使用 Ease，在 LayoutTransitionWallDuration 内推进。
- :71-79 说明该 duration 来自 BarButtonDefaultOperationDurationSeconds；Inkeys/Inkeys/UI/Bar/Bar.Metrics.cppm:25 的值为 0.4 秒。
- WM_LBUTTONUP 在 PageControl.cpp:2353-2369 清除 drag 标志并 ReleasePptDragTracking；:2404 对 pending 请求 pair，:2406 RequestAll；没有终止或立即落定已有 bounds 动画。
- :1895-1902 只要 bounds.active 等条件仍为真，调度器继续渲染，所以即使没有新的 Move，也会继续靠近目标。

确定性时序可由上述代码推导：

1. 最后一次 Move 发布候选 C，因呈现锁忙/窗口失败走 render fallback；
2. renderer 以旧成功几何 B 为 start，向 C 开始 0.4 秒插值；
3. Up 在插值未完时到达；最新候选和所有权被保留，bounds 动画仍 active；
4. 后续帧继续从 B 向 C 运动，形成抬手后的微弱/短暂余移。候选接近 B 时视觉幅度小，符合“微弱”描述，但现场幅度和触发次数尚未测量。

即使 Up 之前两面已被标记提交，动画仍可能继续，见下一节。因此只检查 pending 是否在 Up 后清零，不足以证明已无余移。

#### 3.3 committedLayout 可以早于最终屏幕几何

- renderer 的实际 ULW 来源为 CurrentBounds/Scene presentation；PageControl.cpp:959-971 先应用插值中的 CurrentBounds，:1849、:1929 呈现。
- 其后 :2000-2004 同步提交当前 presentation RECT 和可见性。
- :2052 无论 bounds 是否仍 active，都调用 CommitDragSurface。
- :413-424 的 CommitDragSurface 只转交 frame 的 pair version。
- PageControl.cppm:478-490 的 MarkPptDragSurfaceCommitted 只确认 pending、revision 和两面 mask；满 mask 就把 tracker.layout（目标候选）复制为 committedLayout。:557-563 还核 session/epoch，但仍没有实际 RECT/scale 到达目标的判定。
- :1559-1579 因而可能把“候选 C 已提交”的位置交给 facade，而用户实际看到的是 B→C 的中间帧。

这属于“提交语义与实际几何不一致”的确定缺口；不能以现有 tracker 纯测试通过替代实际屏幕位置断言。它也会削弱取消/保存边沿对“最后成功几何”的假设。

#### 3.4 Up 未采样最后点，是独立缺口

PageControl.cpp:2342 的 Up local 仅在 :2348-2349 转为 Scene logical 并 PointerUp；Move 才在 :2209 调用 UpdateDragLocked。若最终 Up 的屏幕位置与最后一个 Move 不同，候选停在旧 Move。

准确影响：
- 丢失最后一段指针位移，释放位置可能与抬手点不一致；
- 添加正确 Up 最终采样后可能出现一次必要的最终落定，需要与“继续插值”区分；
- 现实现不消费这个点，所以不能把它单独说成“Up 后又推动窗口”的根因；
- 若最后 Move 未超过阈值而 Up 已越阈值，还需要明确是否将这个结束采样转为一次最终 drag，而不是误走 Page 预览；应在审批方案中定清并测试。

WM_TOUCH 的 Up 由 :2143-2154 转译为同一 WM_LBUTTONUP，故也落入此缺口。微软 WM_LBUTTONUP 文档确认 lParam 携带相对 client 的有符号 X/Y，不能只依靠最后一次 Move 来代替。外部来源见文末。

#### 3.5 其他应区分的现象

- 按钮内部 hover/press 的几何或文字动画，不等于整个 HWND 的移动；未来证据应同时比较 HWND RECT、logical body RECT、内容/光影帧。
- DPI/显示快照、配置缩放、真实 presentation outset 和设备 bitmap limit 改变后，runtime resolver 合法重算几何/缩放；不能把这些合理适配全部禁用。
- Down 在 :2284 调 ResolveRuntimePageControlLayout 使用默认预算（10 DIP outset、16384 limit）；Move/render 使用 ResolveGroupBudgetLocked 的真实组预算。极端小屏/大倍率时二者起始拟合可能不同，属于待验证的次要起点风险，当前没有证据证明它解释普通 DPI 下的微移；不应顺手重构预算机制。
- 旧记忆关闭回弹和旧快照覆盖已在前任务用 session/epoch/pairVersion 防护；不能未经新证据再称其为本轮主因。

### 4. 中轴吸附：轴、单位和范围

#### 4.1 生产几何证明是水平中线

PageControl.cppm:987-1051 的 ResolveSurfaceLayout：
- :994-995 仅 MiddleLeft/MiddleRight 为 vertical；
- :1014-1019 用最终控件 scale 计算主体宽高和 5 DIP edge；
- :1032-1035 对 middlePairWidth/middlePairHeight 四舍五入为像素偏移；
- :1036-1037 中间两窗的 x 对称放置；
- :1038-1039 的 y 为 monitor.top + (monitorHeight - bodyHeight)/2 - offsetY。

因此 middlePairHeight=0 将竖向主体中心放到 y=(monitor.top+monitor.bottom)/2，即屏幕水平中线；不是 X 轴屏幕中点，不是工作区或 PPT 内容区域中心，也不是主栏中心。

5 DIP edge 只进入这组的 x，不进入 y。Bar.Scene.cpp:607-614 在 logical bounds 四侧对称扩展同一 outset，presentation center 同样不偏。唯一的正常差异是整数像素奇偶：屏幕高度与主体高度差为奇数时，整数除法可有 0.5 px 中心误差；不要为吸附额外加减 5 DIP 或阴影 outset。

#### 4.2 当前没有 snap

PageControl.cppm:1064-1103 的 ClampPageControlLayout 只做：
- 底组 X/Y 最小最大范围；
- 中组 X 非负范围及 Y 对称上下限。

PageControl.cpp:1383-1416 则从原起点减 dy、clamp、碰撞时保留上一 feasible。ResolveRuntimePageControlLayout（cppm:1126-1228）负责适配和避碰，没有中轴捕获。修改旧 Ppt.cppm 的 ClampPptDrag 不会进入本生产调用链。

#### 4.3 “底栏一样”应对应哪一个值

| 现有策略 | 常量与位置 | 用于什么距离 |
| --- | --- | --- |
| 主栏贴底 | Bar.BottomDock.h:16，BarBottomDockThresholdDip=20.0 | 主体底边到停靠线的距离；:679 乘 environment.zoom |
| 主栏中轴 | Bar.BottomDock.h:17，BarBottomDockCenterThresholdDip=40.0 | 展开低栏主体中心到屏幕竖直中线的距离；:445-464、:512 |
| PPT 底部共享翻页 pair | PageControl.cppm:1041-1049、:1074-1086 | 按用户 offset 放置/裁剪，没有同名 20/40 snap |

主会话明确本次方案按“中轴”对应 40 DIP 规划；这与 20 DIP 贴底不同。两个对象吸附轴方向不同，但可以共享同一数值策略，不代表把 Bar 的整个弹性状态机迁进 PageControl。

Bar 的现有换算也须如实记录：
- Bar.BottomDock.h:293-303 的 ResolveBarBottomDockInteractionZoom = clamp(DPI/96,0.5,4) × configZoom；
- Bar.Interaction.cpp:6726-6729 取成功呈现 snapshot.zoom，:6814-6819 用它构造 environment；
- CenterDragTracker 在 BottomDock.h:512 以 40 × environment.zoom 作物理像素阈值，:540-586 做捕获/脱离/跨带判定。

所以“复用 40 DIP 数值”不意味着所有 Bar configZoom 与 PPT 单独倍率设置下的像素捕获带都完全相同。必须在方案中写出缩放选择，不能再乘一次 DPI。

#### 4.4 待批准的明确建议

- 侧栏 snap 阈值取与 Bar 中轴相同的 40.0 数值，以当前 PPT LayoutMonitor 所选屏幕的 DPI 换算：thresholdPx = 40 × NormalizeDpiScale(dpiScale)。
- 不乘 middlePairScale；侧栏倍率改变控件尺寸，不改变 40 个 Windows 屏幕 DIP 的抓取带。这是本次待用户批准的显式策略选择，而不是谎称 Bar 本来就完全如此。
- 位置 middlePairHeight 已是屏幕物理像素：PageControl.cpp:1382、:1392 直接减屏幕 dy，cppm:1035 直接用作像素偏移；因此比较 abs(clampedCandidate.middlePairHeight) <= thresholdPx，并把候选 Y 设为 0，不能重复除/乘控件 scale。
- DPI 1/1.5/2/3 的计划半宽分别是 40/60/80/120 px；边界包含等号；DPI 超过 4 沿用现有 NormalizeDpiScale 上限，不新增平台政策。
- 只在用户拖动候选（包含合法 Up 最终采样）应用，不在每次页码发布/runtime 自动适配时强制吸附；否则已保存的 10 px 偏移可能在任意页更新时无用户动作跳到中心。
- 保留原绝对 dragStart 基准，不将 snap 后值累加为下一采样起点，避免脱离吸附时漂移。
- 先构造/裁剪原始候选，再尝试中心候选并检查现有 pair 碰撞。中心不可行时保留可行的原始候选或最后 feasible，不推动另一组、不穿越底栏；不能为 snap 绕过已有碰撞合同。
- 当前的 LayoutMonitor（PageControl.cpp:516-530）同时取同一 Display snapshot 的 Primary bounds 和 effectiveDpiX/96；继续该策略，不改成最近 HWND 显示器、PPT 放映屏或跨屏拖动机制。
- 若未来产品要“完整复制 Bar 弹性捕获/脱离动画”或让用户倍率同时扩大抓取带，应另确认；本需求只要求恢复中心吸附和一致的 DIP 距离。

### 5. 最小修复方案建议，待批准后实施

1. 在 PageControl 生产输入链补齐合法 drag 的 Up 最终屏幕点采样，必须在清理拖动状态前完成；使用既有绝对起点、同场/同 epoch 和 latest-wins 版本合同。Page 阈值内 click/透明区域/Arrow/Whiteboard/Cancel 不得被误当成结束拖动。
2. 使拖动候选的 render fallback 直接落定该 pair 的目标几何，保持普通显隐、PPT/Whiteboard 形变、配置/DPI 动画的原行为。不能只测试 surface.dragging：同 pair 的另一面未设此标志，Up 后两面也会清 false；必须按 pair 当前候选 revision/ownership 区分包含 released pending 的拖动几何。
3. 确认两个 surface 的实际成功几何已等于此候选，才推进 committedLayout/完成 handoff。直移本来就是两窗成功后一次提交；render 也不能拿首个动画中间帧的同版本当作最终位置。显隐/新场次/取消仍走已有 rollback 或代次失效。
4. 在同一候选解析中加入中组 Y=0 的 40 屏幕 DIP snap。共用 Bar 中轴常量时可沿用现有依赖/include边界，若需导出纯数值则以最小改动为限；不搬运 Bar 完整 tracker，不另建新拖动系统。
5. FlushCommittedDrag 保持锁外，persist 字段在该处仍代表运行期位置交接；拖动/Up 不直接写配置，保存边沿合同保持。
6. 若有锁链修复，与该研究的“按 pair 而不是单 surface 区分拖动”方案协调，防止为了止移在 owner 中增加对呈现锁的阻塞等待。完整锁根因/方案以并行研究为准。

建议触及的生产文件只有 PageControl.cpp、PageControl.cppm；Bar 常量接入是否需最小导出由主会话实施设计决定。Ppt.cpp 的已提交 facade 边界应尽量保持。旧 Ppt.cppm helper 不作为生产修复靶点。测试优先扩展既有 page_control_tests.cpp 和现有 PageControl 内部离屏入口，不新建构建系统。

### 5.1 常量归属、最小接线和测试接缝

- 保留唯一现有常量 Inkeys::UI::Bar::BarBottomDockCenterThresholdDip，定义仍在 Bar.BottomDock.h:17。它目前位于普通 header/global module fragment（Bar.Main.cppm:14），不是可依赖“import Inkeys.UI.Bar 就必然导出可见”的公开 Metrics 常量。
- 最小接线是在 PageControl.cpp 的 global module fragment 中 include 既有 ../Bar/Bar.BottomDock.h；该 cpp 已 import 完整 Inkeys.UI.Bar（:35），且已使用同目录的 Bar.DirtyRegion.h（:15）。此 header 仅 constexpr、结构和 inline 策略，无启动窗口、注册输入或运行期静态启动逻辑；新增的是编译期 header 依赖，不新增模块、初始化或工程配置。
- PageControl.cppm 的纯候选/snap 函数显式接收 snapDistanceDip 参数，由真实生产调用点传上述常量。page_control_tests.cpp 参照 bar_bottom_dock_tests.cpp:14 include 同一 header，传同一个真实常量并核数值/边界。不要在 PageControl.cppm 再独立硬编码另一个 40，也不必搬动全部 Bar.Dock 常量、改 Bar.Metrics、新建共享配置或新公共层。
- 拖动几何动画的纯决策放在 PageControl.cppm 现有 ShouldAnimateWorkspaceLayout / ShouldLockSurfaceInput 等策略附近。输入是同场/同pair/revision的拖动候选身份与原workspace/显隐条件，输出仅决定该位置是否立即落定；不在 helper 内取锁、操作 HWND 或访问全局 tracker。
- 状态采集继续遵守 dragCommitMutex -> snapshotMutex 顺序；不能为了给 RenderSnapshot 增加候选标志，在现有 SnapshotForRender 持 snapshotMutex 时再取 dragCommitMutex。具体接线需与锁研究统一设计。
- 保持 AnimatedBounds / RetargetBounds / AdvanceBounds 私有在 PageControl.cpp。为生产余移补证，应扩展同文件已有 RunOffscreenTests，从模块内部调用真实 bounds/configure/commit 路径并检查中间帧与最终几何，不仅在 headless 中测一个新 bool helper。实际 Up 的 client/screen 转换与 HWND 轨迹，可在后续许可的 RunHiddenWindowTests 扩展验证。
- 因而最低文件集为 PageControl.cpp、PageControl.cppm、InkeysHeadlessTests/page_control_tests.cpp；可能仅添加测试侧 header include。Bar.BottomDock.h、Bar.Metrics.cppm、Ppt.cppm、项目文件和配置默认不需改。

### 6. 有意义的验证点，当前均未运行

| 层级 | 场景 | 必须断言的结果 |
| --- | --- | --- |
| 生产候选解析 | Bottom/Middle × Left/Right，最后 Move 与 Up 不同 | 最终候选来自合法 Up；同场版本单调；阈值内 Page 短按仍预览，Arrow 不拖动 |
| 真实 bounds 提交逻辑 | 最后 Move 遭 try_lock 竞争，立即 Up，之后无新 Move | released pending 保留、旧 revision 不回拉；第一帧完成最终落定后后续多帧 RECT 不再改变 |
| 已有动画中的 Up | fallback 已呈现首个中间帧，再 Up | 不能沿原 0.4 秒继续拖动插值；没有“mask 满就提前 committed”的假通过 |
| 两窗部分失败 | 第一窗成功第二窗失败、rollback 或随后 retry | committedLayout 不提前推进；恢复后两窗均到最终目标 |
| 连续拖动 | 第一段 Up 尚有 pending，立即第二段 Down/Move | 正确起点/版本，不被上一段 handoff 覆盖；左右两面同步 |
| 位置合并 | 同场旧快照、跨 session/epoch、两 pair 交替 | 现有 pair version/所有权屏障继续有效 |
| snap 纯策略 | Y 偏移 ±39.9/±40/±40.1 DIP、上/下跨带、Up 才进入阈值 | 阈值内归 0、外部保留自由值；左右同 Y，X 不做屏幕中央吸附 |
| DPI/倍率 | DPI1/1.5/2/3，middle scale0.5/1/2.5/3；负原点/奇数屏幕高度 | 阈值仅随 DPI；倍率不改变抓取带；中心误差不超过正常 0.5 px；阴影不参与中心 |
| 碰撞 | 中心被 bottom pair 占用、只有 middle pair、极小屏 | 不重叠、不推另一 pair；中心不可行时仍能合法移动 |
| 布局/工作区 | PPT→Whiteboard、显隐、倍率/DPI更新、CancelCapture | 真实切换动画/输入锁保留；无伪造位置 commit，无拖动写盘 |
| 实机，另获授权 | 鼠标/触摸快速/慢速、原问题设备、双侧同时可见 | 同时观测 HWND RECT、body RECT、实际按钮可点，不以纯 helper 或 headless 通过代替 |

既有 InkeysHeadlessTests/page_control_tests.cpp:235-300 只验证 PptDragCommitTracker 的 pending/release/mask；其中 :267-275 用布尔值模拟锁不可用，没有执行 owner/渲染线程竞争、ConfigureSurface 或真实动画几何。因此已有测试不能排除此处余移，新增测试要覆盖生产 bounds/提交路径。

PageControl.cpp:2562 起存在屏外真实四 HWND、ULW、Window Service 的 RunHiddenWindowTests，可在后续获得相应执行许可时扩展以验证实际窗口位置。它创建 HWND，不能在本轮“只调查、不运行 GUI/测试”授权下执行。纯策略与真实 Scene 离屏验证可纳入批准实施后的最小自动验证，真实输入/Office仍另按权限确认。

### 7. 历史与版本界定

主会话于本轮提供只读 Git 证据：
- 579d1c5e（2026-08-26，stabilize PPT interactions）为 20261010b 发布准备 a50ad143、20261014a 对应 5d319bbf、当前 HEAD 的祖先。
- PageControl.cpp 与 PageControl.cppm 在 a50ad143 和 5d319bbf 的 blob 相同。
- 最近 0eb1c7c5 在 PageControl 只增加 pageAckEpoch 的传递/比较，没有修改拖动、吸附或锁。

本角色未运行 Git，以上历史由主会话负责核验。用户反馈版本可能为 20261010b 或 20261014a，尚未确认，不能把源码相同当成现场已测结论。

已读历史文档提供的补充：
- 09-25-ppt-ui3-scene-and-page-sync/research/layout.md:7-15 记录记忆关闭回弹、完整旧快照覆盖、全局 revision 误当 pair revision、DPI 与倍率重裁剪等旧缺陷及修法。当前链上对应屏障已可见。
- 10-14-toolbar-order-expansion/research/drag-release-third-light.md 调查的是 Bar 第三光源在松手重基准的闪烁，不等于整个 PageControl HWND 的余移；drag-light-implementation.md 记录当时修复。
- 08-23-ui3-bar-drag-jitter-collapse-damage/break-loop.md 说明 Bar 的直移、成功快照与 release tuple 曾有独立呈现问题。不能据此直接移植整套 Bar 事务架构作为本次最小 PageControl 修复。

## Files Found

| 路径 | 职责 |
| --- | --- |
| Inkeys/Inkeys/UI/PageControl/PageControl.cpp | 生产四 surface 的 pointer 输入、候选发布、直移、bounds动画、渲染/窗口提交和位置 handoff |
| Inkeys/Inkeys/UI/PageControl/PageControl.cppm | 生产布局/裁剪/适配/碰撞、纯 tracker、版本门和回调接口 |
| Inkeys/Inkeys/UI/Ppt/Ppt.cpp | facade 发布与已提交 pair 接收，不逐拖动写盘 |
| Inkeys/Inkeys/UI/Ppt/Ppt.cppm | PositionState、旧布局 helpers；不能替代生产 PageControl resolver |
| Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h | 主栏20 DIP贴底与40 DIP居中策略、DPI/configZoom换算 |
| Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp | 主栏实际成功呈现zoom与dock环境构造 |
| Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp | 真实 logical/presentation 对称outset、Scene bounds/命中 |
| Inkeys/Inkeys/UI/Bar/Bar.Metrics.cppm | 共享0.4秒操作动画时长 |
| InkeysHeadlessTests/page_control_tests.cpp | 既有纯tracker/布局/绝对目标/版本测试，缺少真实Up/插值收尾组合断言 |
| .trellis/tasks/archive/2026-09/09-25-ppt-ui3-scene-and-page-sync/research/layout.md | 上轮位置所有权、保存和有效缩放修复历史 |
| .trellis/tasks/archive/2026-10/10-14-toolbar-order-expansion/research/drag-release-third-light.md | 主栏光影在release坐标重基准的独立问题 |
| .trellis/tasks/archive/2026-09/08-23-ui3-bar-drag-jitter-collapse-damage/break-loop.md | 主栏历史直移/帧提交组合验证经验 |

## Related Specs

- .trellis/workflow.md：本轮保持 planning；研究结果持久化，未经批准不实施。
- .trellis/spec/native-desktop/index.md：先识别真实生产UI3与window/render ownership；保留证据等级。
- .trellis/spec/native-desktop/cpp-conventions.md：同一显示快照、实际同步入口、最小diff及CRLF。
- .trellis/spec/native-desktop/rendering-and-ui.md:429-439：Page/DragHandle阈值、纯平移NOZORDER、pair碰撞、真实组预算、latest-wins、try_lock、取消/提交和释放合同。
- .trellis/spec/ppt-interop/native-session-ui3.md:42-47：facade/候选/成功几何的单pair版本交接、拖动不写盘、DPI/有效缩放、主屏同源快照。
- .trellis/spec/ppt-interop/native-session-ui3.md:55：拖动/渲染不做PPT焦点交接。

## External References

- Microsoft Learn, DPI and device-independent pixels，访问日期2026-10-18：
  https://learn.microsoft.com/en-us/windows/win32/learnwin32/dpi-and-device-independent-pixels
  支持 Windows DIP 基于96 DPI、像素与DIP按DPI/96转换，窗口/鼠标坐标使用像素。本报告40×DPI/96是待批准产品选择，不能由此声称Bar现有configZoom已被移除。
- Microsoft Learn, WM_LBUTTONUP message，访问日期2026-10-18：
  https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-lbuttonup
  支持 Up 的lParam包含client坐标，GET_X_LPARAM/GET_Y_LPARAM保留有符号值。此文档不证明具体用户输入时序或现场余移根因。

## Caveats / Not Found

- 未取得故障日志、转储、逐帧RECT或实机重现。源码时序缺口与用户实际根因必须区分。
- 用户补充“只有被拖控件停住，主栏仍可操作”，这对全局UI owner/render死锁有约束；本报告不把fallback插值当成该卡死的根因。完整锁链另由并行研究给出。
- 角色只写本文件；没有读取implement.jsonl/check.jsonl，没有运行Git、构建、测试、HWND/GUI/Office或修改规范。
- 外部文档未用于承诺PowerPoint/WPP/MSO版本兼容。
- 引用行号以基线当前源码为准；后续实施可能移动。

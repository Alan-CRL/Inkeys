# UI3 底栏吸附切换跳变与提示裁剪修复 — 技术设计

> 2026-10-26 当前结果（覆盖下方历史停止点和自动检查计划）：用户已人工确认竖直吸附脏区外残影修复。顶边回归修复与 a7dc3a08 清理已实现并完成静态交叉审查；撤掉错误抓点约束及临时 trace，保留成功呈现保护。按用户要求未编译、未运行自动测试或GUI，等待其人工验证；脏区内跳变未宣称解决。取舍见 `research/top-edge-fix-review-2026-10-26.md`。

> 2026-10-26 最新停止点：用户要求“验证完成后进行最小修复，然后继续的验证等到下一轮”。已完成原症状实机复现、录像观察及第一轮同步trace判因；仅删除空presentDirty兜底的晚ForceFullDamage，保留整窗绘制和默认关闭诊断。修复后构建、自动回归、实机复测尚未执行，明确留下一轮；上边缘回归尚未改动。完整证据与后续接力见 `research/live-reproduction-2026-10-26.md`。

> 2026-10-26 当前阶段（覆盖下方历史完成/未授权说明）：用户要求延续本任务，先通过真实可见窗口复现“剧烈抖动/位置闪烁”，确认观察到相同症状后才开始修复。已授权原生/命令行测试与桌面输入，明确不使用 Computer Use。上边上移、窗口尺寸变化和测试 PASS 均不能单独代替原症状复现。当前执行依据见 `research/live-reproduction-2026-10-26.md`。

> 2026-10-26 接力：本计划已获本会话授权并完成代码与自动化，最新证据见父任务 `research/final-integration-validation.md`；原文未来式保留作历史设计。GUI/真实系统呈现验收待授权，任务未归档。

## 证据结论与边界

当前普通捕获已经有成功底边 seed、首帧不积分、deferred barrier 和最终 ULW 配对；居中 Free/Centered 也有成功远端重基准。9 月“旧浮动帧冒充新 serial 后被直移到 dock”的固定序列已被现有代码封堵，不能原样套用旧根因。

静态调查没有唯一解释本次所有帧的证据，但直接复核确认了三个可构造反例的几何缺口。设计将“确定需要修复的缺陷”与“仍需逐帧判因的分支”分开；任务最终完成仍必须以用户症状消失为准。

| 项目 | 当前证据 | 本设计处理 |
| --- | --- | --- |
| BottomDocked→Floating 脱离首帧 | 从输入 offset 播种并同帧积分，capture-bottom 也继续积分；未从最后成功映射交接（`Bar.RenderLoop.cpp:6963-6981,7002-7008`） | 确定修复：成功形状 seed + 首个成功候选不积分 |
| 释放吸收后的成功快照 | `RebaseBottomDockPresentedWindow` 只改 translation/中心，未改两轴 base/visual 端点（`Bar.Main.cppm:1010-1045`；调用 `Bar.RenderLoop.cpp:12848-12899`） | 确定修复：区分布局吸收与真实屏幕直移，完整重基准 tuple |
| 提示/窗口最终包含性 | 提示有包络预留，但 capacity 与完整 predicted envelope 分两轮求解，最终没有 `pptSrc + psize ⊆ target` 检查（`Bar.RenderLoop.cpp:8825-8842,9330-9389,12306-12327`） | 确定修复：统一预测与容量，并在 GetDC/ULW 前验证真实 target 包含 |
| 实际局部抓点与中心代理 | 实际 MainButton/Logo Y 走 body 仿射，代理只走刚性 offset，可构造局部点误差 | 条件分支：只有帧证据指向它才单独设计最小改动，不恢复历史大补丁 |
| dirty、glyph overhang、真实 ULW/DWM | 正常提示包络和 dirty 接线较完整，无法静态唯一裁决 | 同帧测量后分别修，不用扩窗代替 dirty，也不用 dirty 代替窗口 |

详细证据见 `research/dock-transition-clipping-investigation.md`。

## 总体原则

1. 保留现有 serial CAS、deferred barrier、普通捕获 seed、水平远端 seed 和动画曲线，除非真实帧证明其仍被绕过。
2. 所有连续性以最后一次**完整成功呈现**为旧基线；失败或跳过候选不能消费首帧交接或推进快照。
3. 窗口位置、内容映射、source/size、dirty 和真实 HWND 必须表示同一 candidate frame。
4. 不以关闭吸附/动画、放大 24 DIP 限制、隐藏一帧、无限扩窗或恢复 `ed6012fc` 大补丁作为修复。

## A. 真实生产链复现与判因接缝

优先复用 `CreateBarOffscreenRenderCallback` 和现有交互 worker，驱动实际 `Seek` 与 RenderLoop，只把最终 ULW 替换为记录 sink。拟在现有 UI3 presentation harness 增加 `--ui3-bottom-dock-transition-test`，不新建平行测试框架。

每个候选和成功帧记录：两轴 mode/phase、输入/帧/成功 serial、普通与 capture-bottom `e/c` 及速度、dt、frame/presented translation、indicator envelope、capacity origin/size、真实 target size、source/size/destination、presentDirty 和提交结果。保留异常前后至少三个成功帧及被跳过候选。

先构造三个确定反例，再检查用户报告的进入/退出与水平序列：

- 若异常是旧位图被 `SetWindowPos` 直移，查 barrier/所有权；
- 若 ULW destination 跳而映射连续，查窗口 tuple；
- 若 destination 正常但端点残差突变，查脱离 seed 或吸收快照；
- 若 envelope 超 candidate/source，查 viewport/capacity；
- 若 candidate 完整但 D2D/ULW dirty 缺顶部，查 dirty；
- 若软件矩形全部正确而屏幕仍异常，转真实 ULW/DWM 验证。

现有 raw capture 字段不足时，才按其既有有界、默认关闭合同补充数值字段；不增加逐帧阻塞磁盘日志。

## B. 脱离首帧成功形状交接

### 当前反例

捕获方向从最后成功底边 seed 且首帧不积分；脱离方向却从当前输入 `inputOffsetDip` 播种并立即推进普通弹簧，capture-bottom 也继续推进。现有 `CheckDetachContinuity` 人为使用相同 `e/c` 和 `windowShift=e*zoom`，恰好排除了候选/成功差异、首帧 dt 和失败重试（`InkeysHeadlessTests/bar_bottom_dock_tests.cpp:1362-1390`）。

### 修复合同

1. 仅由 `presented BottomDocked -> candidate Floating` 触发脱离 seed；在首次成功呈现前，失败/作废候选始终从同一旧成功映射重新 seed。
2. 新纯 helper 以成功快照的两轴映射、候选 base geometry、zoom、monitor origin 和 frame translation 为输入，求普通弹簧与 capture-bottom 的位置/必要速度；不能直接复用未经校准的 input offset。
3. 首个脱离候选只 seed，不推进普通或 capture-bottom 弹簧；下一张成功帧之后才按现有曲线积分。
4. 初值可超过普通 24 DIP 保护范围，使用现有 `preservePresentedOffset` 语义连续恢复，不扩大阈值或截断旧像素。
5. 保留当前指针样本产生的正常窗口位移，测试断言的是扣除该正常位移后的额外屏幕残差。

在 base 几何不变时，令旧 Docked 的普通/底端偏移为 `e0/c0`，新 Floating 为 `e1/c1`，窗口位移差为 `ΔT=T'-T`。顶端连续要求 `ΔT=zoom*e0`；底端连续要求 `c1-c0=e1-ΔT/zoom`。当前首帧积分和候选污染会破坏这些关系。生产 helper 应使用完整屏幕端点公式，以覆盖 base 改变，而不是只把该简化公式硬编码进测试。

动画关闭继续遵循现有即时完成语义；动画开启时不改变频率、阻尼或目标曲线。首次行为变化需 A/B 检查回弹手感。

## C. 释放吸收后的完整快照重基准

吸收量 `A` 被加入主按钮布局 `A/zoom`，实际 residual translation 变为 `T-A`，真实 HWND 不因吸收本身移动。要保持 `screen = origin + layout*zoom + translation` 不变，快照中的绝对 layout 端点也必须同步增加 `A/zoom`；只改 translation 会产生 `-A` 的假屏幕位移。

### 修复合同

- 为 `RebaseBottomDockPresentedWindow` 增加显式、与 `screenDelta` 不同语义的 layout delta（或等价专用 absorb 入口），调用处传入 `A/zoom`。
- 纵向平移 `baseTop/baseBottom/visualTop/visualBottom/rigidGripY`；横向平移 `baseLeft/baseRight/visualLeft/visualRight`。scale、弹性 offset 和 rigid translation offset 不作为绝对坐标平移。
- 吸收本身没有改变真实屏幕，因此 screen center 和屏幕 indicator bounds 不额外加 `screenDelta`；真实 `SetWindowPos` 调用仍只传物理 `screenDelta`，不伪造 layout delta。
- 明确 indicator/hit bounds 的坐标语义：真实直移应随屏幕平移，布局吸收应保持屏幕不变。快照 serial 的奇偶写事务和 `directWindowDragMutex` 保持不变。
- 不用移动真实 HWND 补偿内部坐标，不把下一次完整呈现当作修复快照的手段。

回归必须覆盖吸收后立即再次按下、下一次完整呈现前进入/退出吸附、正负两轴、非零 monitor 原点、不同 zoom、命中往返和失败重试。

## D. 提示、预测 viewport 与最终容量包含

### 保留现有正面机制

继续复用 `ResolveBarBottomDockIndicatorVisualEnvelope` 的 Back/描边/Gaussian/AA 包络、首次 reveal damage、文案扩宽/缩窄顺序和 idle viewport 收缩。窗口不足与 dirty 遗漏分别验证。

### 统一求解顺序

1. 在最终容量决策前完成当前纯几何的完整 predicted envelope / candidate viewport 求解；不把 D2D 绘制提前，只重排无副作用的几何阶段。
2. capacity 的 required bounds 必须包含该完整预测范围以及现有提示/dock 包络，再调用 `ResolveBarWindowCapacity` 和 `EnsureDeviceResources`。保留稳定锚点、高水位与保守扩容，不每帧重建或缩窗。
3. 以最终 `capacityOrigin` 重新计算 `candidateSource`，映射改变继续触发整窗替换和完整 damage。
4. 在 GetDC/ULW 前同时验证：`indicatorEnvelope ⊆ candidateViewport`（提示活跃/预留时）、`source.x/y >= 0`、`source + size` 不超过**实际 target bitmap size**，以及 `pptDst/psize` 与同帧 translation 一致。
5. 包含检查失败时不得钳 source 后提交裁剪帧，也不得推进成功快照。应保留完整 dirty，按有界路径扩容/重算或 Retry；不能形成无限热重试。

### 分支判因

- candidateViewport 未含提示顶部：修对应动画 range / predicted envelope 输入。
- candidate 完整但 presentDamage 缺旧/新提示：修稳定视觉 key 失效和 damage，不扩大窗口代替。
- 矩形包络完整但文字上缘仍裁：单独测量真实 glyph overhang，不能把 DWrite 普通 metrics 当作已覆盖。
- 软件矩形均正确但蓝框/屏幕仍异常：核对真实 `GetWindowRect` 与 ULW/DWM，不宣称纯几何已解释。

## 条件分支：真实局部抓点

只有帧证据排除上述原因并指向代理抓点时，才评估按下时保存成功图像内归一化抓点、再由实际仿射求新旧形状的方案。MainButton/Logo/MainBar 必须保持同一模型，且单独审查命中、光影和回弹手感。历史上类似扩展曾被回退，因此该分支不得与上述三个窄修复混成一个大补丁。

## 回退与集成

- 脱离 seed、吸收重基准、容量包含各自形成独立可回退变更单元和失败回归。
- 不重写普通捕获、水平远端 seed 或 serial/barrier；若真实帧证明仍有绕过，先记录新序列再追加最小修复。
- 与居中子任务共享 `Bar.RenderLoop.cpp`、`Bar.BottomDock.h` 和 offscreen harness，实施不得并行改同一文件；父任务最后重跑动态居中 + 吸附 + 提示组合场景。

## 当前最小变更边界：顶边目标与 commit 审查

- 行为缺口位于纵向映射：底边弹簧恢复时按固定抓点反推 top，违反顶边只随输入平移的设计。修复位于现有 Seek → 同代 tuple → RenderLoop → 成功快照链。
- Seek 按下从同一成功 visualTop 初始化顶边屏幕目标，之后只累计有效输入差；用现有 pending double 槽替换 effective-pointer 语义，删除跨线程 grabBaseRatio，不新增成功输入历史或锁。
- RenderLoop held 使用该 top，交接按目标 top 与成功 top 差搬运整个成功形状。恢复统一为 top-offset 与独立形状弹簧（Docked底端偏移，Floating高度偏移），删除 1/(1-r) 与 bottomGrab 奇点特判。松手从成功形状接续，不把未上屏候选当基准。
- 必要文件：Bar.BottomDock.h（简化映射/交接helper）、Bar.Interaction.cpp（发布top、撤临时trace）、Bar.Main.cppm（删除比例槽、同槽明确语义）、Bar.RenderLoop.cpp（消费top/统一恢复/撤trace）；两份既有测试同步删除错误约束，rendering-and-ui.md纠正合同。
- 保留已人工确认的empty-dirty修复、source/target包含、串行屏障和成功呈现/命中事务。审核a7dc的其他内容只删除可证明冗余或由错误抓点模型产生的代码，不回退已存在的防护。
- 不扩展窗口预留、不改变弹簧频率/阻尼、不做其他性能重构、不创建新测试框架；所有运行和视觉结论留给用户人工验证。静态审查只确认结构与调用一致性，不能记作运行PASS。

# Research: UI3 底栏吸附切换跳变与提示裁剪

- Query: 追踪当前参与编译的 UI3 吸附判定与状态切换、拖动/释放、目标位置、果冻首帧、窗口外框/位置/尺寸、相对内容偏移、ULW 呈现、脏区，以及“底栏模式/居中”提示测量和动画；结合 Git/Trellis 历史确定现状、根因证据、验证与修复边界。
- Scope: mixed（当前代码、测试、Git 与 Trellis 历史；无需外部资料）
- Date: 2026-10-25
- Status: 静态调查完成；真实逐帧 GUI/ULW 验证仍待主会话或后续获授权阶段执行。

## 结论与证据等级

### 总结论

1. **当前实现不是“缺少吸附首帧保护”这么简单。** 普通 `Floating -> BottomDocked` 已有成功底边种子、首帧不积分、deferred barrier 和最终 ULW 配对；居中 `Free <-> Centered` 也有成功远端重基准。9 月旧问题“旧浮动帧冒充新 serial，随后旧位图被直移到 dock”的固定序列已被现有代码明确封堵。
2. **静态代码仍能证明三组几何缺陷/不保证条件：**脱离首帧没有对称的成功像素交接且立即积分；真实局部抓点与交互代理抓手不同；释放吸收后、下一次完整呈现前的成功快照端点与 translation 不一致。它们可以产生跳变，但尚需实际帧证明哪一项（或其他呈现因素）就是本次用户所见“先到 dock、下一帧恢复”的直接原因。
3. **提示矩形/光影包络、capacity、predicted viewport 与 dirty 的现状接线比问题描述假设完整。** 提示峰值包络明确进入 capacity 和 predicted viewport，首次 reveal 也显式进入 damage。静态代码没有证明正常提示路径必然缺顶部窗口区域，不能宣称“根本未预留提示”。
4. **窗口/呈现仍存在一个确定的最后包含性防护缺口：**生产没有最终 `pptSrc + psize ⊆ target` 检查，且 capacity 与完整 predicted envelope 不是同一轮统一求解。历史 helper 已构造出越界反例；现有对应测试却先把预留包络人工放在容量内，无法发现该问题。它是裁剪的合理候选，但在取得本次实际几何前不能定为唯一根因。
5. **现有测试没有复现用户报告的完整生产三帧序列。** 相关 helper 测试有价值，但覆盖的是理想连续性假设。必须通过记录真实候选/成功帧、ULW 参数和实际 HWND/图像，才能把直接根因与这些候选缺陷一一对应。

证据标记：**[现状]** 当前代码直接确认；**[静态反例]** 可由当前公式构造确定反例；**[推断]** 有代码依据但需运行确认；**[历史]** 只说明设计/实机调查演变，不替代当前事实。

## Findings

### Files Found

| File Path | Description |
|---|---|
| `Inkeys/Inkeys.vcxproj` | 当前产品编译登记；UI3 Bar 源文件证据 |
| `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp` | `Seek` 拖动、吸附判定、目标位移、直移、释放与回滚 |
| `Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h` | 两轴 tracker、映射、弹簧、提示几何/包络、serial/barrier 纯逻辑 |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm` | 共享原子状态、成功呈现快照、命中映射与重基准 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | 帧布局、弹簧积分、提示测量/绘制、capacity/viewport、D2D/ULW 与成功提交 |
| `Inkeys/Inkeys/UI/Bar/Bar.WindowGeometry.h` | capacity/viewport、坐标换算、窗口呈现锁 |
| `Inkeys/Inkeys/UI/Bar/Bar.DirtyRegion.h` | 变化键、旧/新 damage、窗口裁剪、成功/重试快照 |
| `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp` | D2D dirty clip、Shape 实际描边/光影绘制 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderingAttribute.cppm` | 普通视觉的描边/Gaussian/AA 外框 |
| `Inkeys/Inkeys/UI/Bar/Bar.DisplayTransition.h` | 稳定任务栏 Y 动画所有权及重定向 helper |
| `InkeysHeadlessTests/bar_bottom_dock_tests.cpp` | 两轴吸附、首帧、提示和弹簧 helper 回归 |
| `InkeysHeadlessTests/window_geometry_tests.cpp` | 直移/resize、capacity/viewport 与坐标测试 |
| `InkeysHeadlessTests/dirty_region_tests.cpp` | dirty 事务、映射变化及光源测试 |
| `.trellis/spec/native-desktop/rendering-and-ui.md` | 当前脏区、二维吸附与呈现合同 |
| `.trellis/spec/native-desktop/ui3-render-diagnostics.md` | raw capture、离屏基准及验证边界 |

### 当前生产编译边界

- 调查基点：`fdda877d1d65dae877ee46cd081f36940eea23f6`，分支 `bugfix/cut`。开始调查时相关产品源码无未提交差异；本研究只新增本任务 `research/` 文档。
- `.trellis/spec/native-desktop/index.md:5` 直接确认 UI3 是唯一悬浮栏入口，旧 `IdtFloating` / 旧 `IdtWindow` 不参与编译。
- `Inkeys/Inkeys.vcxproj:996-1019` 将 `Bar.Initialization.cpp`、`Bar.Interaction.cpp`、`Bar.Main.cpp`、`Bar.RenderLoop.cpp`、`Bar.State.cpp` 等列入 `ClCompile`；`:426` 登记 `Bar.BottomDock.h`。下文不使用旧 UI2 解释现状。

## 1. 实际调用与逐帧数据流

### 1.1 输入、吸附判定与目标窗口位置

1. `BarUISetClass::Seek` 是实际拖动协程（`Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:8654`）。起拖先读取 `BottomDockPresentedSnapshot()`，包含上次完整成功的两轴模式、映射、显示环境、窗口 translation 和指示器命中（`Bar.Main.cppm:531-567,735-839`）。若共享候选已领先成功帧，输入先回退到成功 tuple（`Bar.Interaction.cpp:8697-8758`）。
2. 起拖时从成功主按钮基准中心和 `rigidTranslationDip` 计算抓取偏移（`:8807-8818`），并分别构造纵向与居中 tracker（`:8827-8835`）。纵向捕获阈值是 20 DIP，居中是 40 DIP（`Bar.BottomDock.h:17-18`）。
3. 纵向 `BarBottomDockDragTracker::Update` 使用“正常形态可见底边距 dock 线”判定浮动捕获，使用“刚性抓取点偏离稳定抓手”判定脱离（`Bar.BottomDock.h:704-780`）。捕获时以当前采样反推稳定抓手（`:733-743`），因此输入侧目标主按钮 Y 会变为 dock 中心（`Bar.Interaction.cpp:9145-9149`）。
4. 居中 tracker 使用指针驱动、未形变的联合主体中心；捕获/脱离和高速跨带在 `Bar.BottomDock.h:539-632`，调用在 `Bar.Interaction.cpp:9123-9129`。目标 X 再经主按钮可见边界夹取（`:9137-9144`）。
5. 每个采样先 `BeginBottomDockTransition()` 独占奇数 serial，再写两轴模式、phase、弹性量、屏幕抓手和目标 translation，最后 `FinishBottomDockTransition(modeChanged...)` 发布偶数 serial 和 deferred barrier（`:8885-8945,9182-9204`）。模式变化后禁止当前/后续采样用 `SetWindowPos` 把旧位图直移到新形态目标，直到对应 ULW 成功（`:9199-9204`）。
6. 无 barrier 的普通采样可在 `directWindowDragMutex` try-lock 成功时用 `SetWindowPos`（`:9206-9268`）；失败或锁竞争交给完整渲染。释放先发布 `bottomDockDragActive=false` 和 recovery phase，再结束 direct-drag 所有权并唤醒渲染（`:9327-9370`）。

### 1.2 渲染帧快照与显示位置所有权

- 帧入口可能先吸收 direct translation：只有 direct phase 可从 Idle 原子转为 Absorbing 时才执行；translation 被加入主按钮布局、平移 dirty/cache，并保留真实 HWND 相对新布局的残差（`Bar.RenderLoop.cpp:12848-12900`）。若 release tuple 已到但 direct phase 仍为 Dragging 且有待吸收位移，则本帧 Retry，禁止先呈现释放态（`:13027-13035`）。
- 随后渲染以同一个稳定偶数 serial 读取模式、phase、两轴弹性量、抓手和 translation（`:12993-13026`）。`ApplyDisplayTransition` 在该快照之后执行（`:13050-13075`）。
- 稳定桌面底栏的任务栏 Y 动画只允许在非拖动、非恢复、非场景放置时写 Y（`Bar.DisplayTransition.h:47-69`；`Bar.RenderLoop.cpp:1371-1410`）。拖动期间冻结旧显示过渡（`:1413-1438`）。因此不能把近期任务栏 Y 动画直接当成吸附切换帧的第二写者；其门禁是有意排除拖动/恢复的。

### 1.3 弹簧、首帧和 D2D 内容偏移

1. **普通纵向抓手/恢复：**`Bar.RenderLoop.cpp:6895-6982`。按住 Docked 时从同帧屏幕抓手反推 `bottomDockSpring.positionDip`；非按住或 Floating recovery 时从输入 offset 播种并调用 `AdvanceBarBottomDockSpring`。
2. **捕获下端点：**`:6983-7010` 计算上次成功屏幕底边和新基准底边，`SeedBarBottomDockCaptureBottom` 仅在 `presented Floating -> candidate BottomDocked` 时设置初值（`Bar.BottomDock.h:1522-1536`）。首个 seed 帧不积分；后续帧才推进。
3. **居中：**`:7190-7249` 计算刚性抓手/恢复和远端弹簧。模式改变时用 `ResolveBarBottomDockRebasedFarEdgeOffsetDip` 从成功远端、新旧 translation 和新基线求初值（`Bar.BottomDock.h:1020-1032`）；seed 帧不推进远端弹簧。
4. **最终映射：**`:7250-7292`。Docked 映射为 `visualTop=baseTop+e`、`visualBottom=baseBottom+c`；Floating recovery 为 `visualTop=baseTop`、`visualBottom=baseBottom-e+c`（`Bar.BottomDock.h:1444-1514`）。主栏主体使用完整二维仿射；主按钮/Logo 在 X 刚性、Y 仍使用 body scale/translation；附属面板只平移（`Bar.RenderLoop.cpp:9456-9549`）。
5. **呈现：**layout damage 经 `BarLayoutToClientRect` 转成 candidate client dirty，最终锁内再次核对 serial/barrier/实际 translation，调用 `UpdateLayeredWindowIndirect`（`:12249-12336`）。`ResolveBarBottomDockFramePresentation` 拒绝混合 tuple，且首张形态帧强制使用与位图配套的 translation（`Bar.BottomDock.h:1402-1442`）。成功后一次性推进 dirty、viewport、present mapping、真实窗口 bounds 和成功快照（`Bar.RenderLoop.cpp:12410-12603`）。

### 1.4 提示测量、动画、窗口区域与 dirty

1. 提示高度 30 DIP，竖直中心是**形变后的主栏可见顶边**，所以矩形约 15 DIP 位于主栏顶边之上（`Bar.BottomDock.h:190-229`；`Bar.RenderLoop.cpp:7843-7866`）。水平中心来自主按钮与主栏联合外框，X 使用对应 grip/body 映射。
2. 文案测量使用真实 DWrite format/font collection、NO_WRAP 和 `DWRITE_TEXT_METRICS`，取 `widthIncludingTrailingWhitespace/height` 后除以 zoom（`:7675-7708`）。普通/居中标签选择与淡出保留在 `:7043-7074`。
3. 宽度变化串行执行“先扩框再换字 / 先换字后缩框”；隐藏时直接准备正确文案和宽度（`:7711-7837`）。显隐进度使用 EaseOutBack/EaseInBack（`:7028-7035`），动画 `changed/active` 经 `ChangeValue` 汇入 `needRendering`，再形成 visual demand（`:5023-5027,7092-7106,7545-7549`）。
4. `ResolveBarBottomDockIndicatorVisualEnvelope` 把中心缩放、缩放描边、Gaussian 和 AA 纳入统一像素包络（`Bar.BottomDock.h:285-318`）。当前 actual dirty 和 Back 峰值 reservation 都使用该函数（`Bar.RenderLoop.cpp:7888-7920`）；首次 reveal 即使 scale 尚为 0，也显式 `IncludeDamage` 峰值包络（`:7959-7964`）。
5. actual bounds 进入 visible content（`:8280-8293`）和稳定 dirty key（`:8763-8767`）；主栏果冻变化也会标记提示 key（`:7311-7327`）。峰值包络进入 capacity content（`:8825-8835`）和 predicted viewport（`:9330-9346`）。
6. D2D 在 `SetBaseTransform()` 后以同一 `presentDirtyRect` 建立 clip/Clear（`:9457-9460,9550,9580`；`Bar.Rendering.cpp:462-470,487-508`）。提示在主栏/面板后、调试覆盖前绘制（`Bar.RenderLoop.cpp:12057-12142`）。ULW `prcDirty` 来自同一 candidate damage；source/size/mapping 变化时改为整窗替换（`:9376-9440,12319-12327`）。

## 2. 跳变：静态可确定的机制、反例与未证实部分

### 2.1 先排除把 9 月旧序列原样当成现状

**[历史]** `342990fe` 的任务记录了与本次非常相似的三帧序列：旧 Floating 画面 S100；输入捕获 S102/barrier102；旧渲染错误写回 Stable 并冒充 S104 后提交旧画面；输入误认为捕获已上屏，下一次轻移把未形变旧位图 Y+20 直移到 dock；随后正确位图恢复抓手。

**[现状]** 这条固定路径已被三层改变封堵：

- 写者以 CAS 独占 serial，渲染自动写回只能归位自己消费过且未拖动的 serial（`Bar.BottomDock.h:1354-1382`）；
- 模式切换 deferred barrier 与两轴 tuple 同发（`Bar.Interaction.cpp:8934-8936`）；
- 最终 ULW 对陈旧/发布中/更新 barrier 的候选进行拒绝或配套提交（`Bar.BottomDock.h:1421-1442`；`Bar.RenderLoop.cpp:12272-12327`）。

现有 `TestOldFloatingFrameCannotAcknowledgeCapture` 正是该序列的回归（`InkeysHeadlessTests/bar_bottom_dock_tests.cpp:150-219`）。因此，**若本次仍看到完全相同的旧位图 dock 帧，必须重新验证是否有新的绕过路径、真实 OS 表现或未覆盖交接，不能再次把旧根因当结论。**

### 2.2 [静态反例] 脱离首帧没有对称的成功像素交接

这是当前最直接的代码级连续性缺口。

捕获方向使用成功底边 seed，而脱离方向在 `Bar.RenderLoop.cpp:6963-6981` 中：

- 从当前输入 `inputOffsetDip` 设置普通弹簧，而不是从 `presentedDockSnapshot.mapping` 反推；
- 同帧立即调用 `AdvanceBarBottomDockSpring`；
- 已活跃的 capture-bottom 也在 `:7002-7008` 同帧继续积分；
- 没有用成功帧重新校准这两个候选状态或速度。

在固定 origin、zoom 和 base 几何下，令旧 Docked 为 `top=a+e, bottom=b+c`、窗口为 `T`，新 Floating 为 `top=a, bottom=b-e'+c'`、窗口为 `T'`：

```text
顶端连续要求：T' - T = zoom * e
底端连续要求：T' - T = zoom * (e' - (c' - c))
```

输入通常按当前 raw 抓手令 `T'-T≈zoom*e`，所以要同时连续，至少要求 `e'=e` 且 `c'=c`。当前代码并不保证：

- `dt>0` 时首帧已经积分，`e'/c'` 与旧成功值不同；
- 之前有失败/跳过候选时，render state 与成功快照不同；
- 普通 offset 被限制到 24 DIP，而 raw HWND 可跟随更大的实际输入位移（`Bar.BottomDock.h:759-769,1545-1551`）。

**结论：**该不对称可以构造确定的底边跳变，且覆盖“退出吸附”场景；现有理想测试 `CheckDetachContinuity` 人为令 `windowShift=e*zoom` 且两个映射 `e/c` 相同（`InkeysHeadlessTests/bar_bottom_dock_tests.cpp:1362-1390`），恰好排除了上述风险，不能证明生产首帧连续。

**边界：**这证明“当前代码不保证脱离连续”，不等于已证明用户每次脱离都走该错误条件。需要记录 `dt`、候选/成功 `e/c`、实际 translation 与跳帧历史，区分正常指针移动和额外残差。

### 2.3 [静态反例] 真实局部抓点与代理抓手不同

- 输入把抓取代理解释为“未映射主按钮中心 + `rigidTranslationDip`”（`Bar.Interaction.cpp:8809-8818`），渲染直接抓手也按该代理求 `e`（`Bar.RenderLoop.cpp:6947-6961`）。
- 但主按钮/Logo 的实际 Y 绘制仍使用 body `scaleY/bodyTranslationY`（`:9482-9485`），所以真实局部点走 `mapping.MapY`，不是简单的 `base + e`。
- 对归一化位置 `r` 的被抓点，其形变位移为 `(1-r)*e + r*c`；代理使用 `e`，差为 `r*(c-e)*zoom`。例如现有测试本身同时断言 `e=-20` 时 `rigidGripYDip=20`、`MapY(40)=30`（`InkeysHeadlessTests/bar_bottom_dock_tests.cpp:1108-1117`），直接展示中心代理与真实中心不同。

**结论：**当前实现不能声称实际按住的局部点始终跟手。它可解释果冻形变/底边异常及快速切换放大，但不能仅凭这一点推出“先出现一张稳定 dock 终态帧”的精确时序。

### 2.4 [静态反例] 释放吸收后的成功快照短暂不自洽

吸收时主按钮根布局增加 `translation/zoom`，实际 translation 变为 `presented-Absorbed`（`Bar.RenderLoop.cpp:12860-12898`；`Bar.WindowGeometry.h:296-303`）。随后调用 `RebaseBottomDockPresentedWindow`（`Bar.Main.cppm:1011-1045`），但它只更新 translation 和中心字段，**不平移 `mapping.baseTop/baseBottom/visualTop/visualBottom` 或水平端点**。

因此，若原成功映射端点为 `V`，吸收量为 `A`，真实窗口未因“吸收”本身移动，但快照公式会从 `O+V*z+T` 变为 `O+V*z+T-A`。下一次完整成功帧会重建一致快照；在那之前，新输入/新捕获可读到不一致 tuple。

**结论：**坐标缺陷本身确定；是否在本次异常帧前被消费，必须结合实际吸收与下一次手势/帧的交错记录。不能用“有 mutex”推导快照内容已经一致。

### 2.5 进入吸附、水平居中与“dock 终态闪回”的未证实部分

- **进入吸附：**`c=(oldBottom-newBaseBottom)/zoom` 使首个候选的新底边等于旧成功底边，且首帧不积分（`Bar.RenderLoop.cpp:6987-7008`）。顶端/抓手则跟随当前有效输入，不是简单复制旧画面。这条路径在静态理想条件下有明确连续性设计。
- **水平居中：**远端 seed 使用成功远端、旧/新 translation 和新基线，seed 帧不积分（`:7212-7249`）。水平主按钮 X 为刚性映射，缺少纵向那样的 body-scale 抓点差异。正常路径不能笼统归因为“水平没有首帧保护”。
- **共享风险：**水平同样受释放吸收快照不一致影响；显示环境变化、候选/成功交错和动态根布局也可能改变 continuity 的代数前提。
- **仍属推断：**本次是否发生 barrier 未覆盖的提前直移、首个候选与成功像素不一致、OS/ULW 实际目的地与软件日志不一致，或窗口 resize/source 重解释导致的视觉闪回。静态代码无法在这些选项中唯一裁决，更不能把“窗口尺寸变了”或“弹簧本身坏了”直接写成根因。

## 3. 裁剪：窗口区域、dirty 与可确定的防护缺口

### 3.1 正常提示路径的正面证据

**[现状]** 提示不是只在绘制阶段临时出现而忘记预留：

- 实际矩形和 Back 峰值都有统一包络（`Bar.BottomDock.h:285-318`）；
- 峰值包络被并入 `capacityContentBounds`，随后才调用 `ResolveBarWindowCapacity` 和 `EnsureDeviceResources`（`Bar.RenderLoop.cpp:8825-8842`）；
- 同一峰值包络在非零动画范围时进入 `predictedEnvelope`，再进入 viewport controller（`:9330-9359`）；
- 首次 reveal 已进入 damage，后续 actual bounds 由稳定 key 记录旧/新并集（`:7959-7964,8763-8767`；`Bar.DirtyRegion.h:115-179`）。

`ResolveBarWindowCapacity` 以主按钮为锚，根据 requiredBounds 到锚点的最大 reach 扩容并做包含复查（`Bar.WindowGeometry.h:350-397`）。对提示自身的这份 requiredBounds，静态顺序是先纳入再分配，不是事后才想起提示。

**结论：**不能由现有代码直接推出“提示顶部必然超窗”。若实际蓝框不足，需要打印当帧完整几何，检查是否还有其他预测分支超出、缓存/映射不一致或 Windows 实际窗口与 candidate 不一致。

### 3.2 [静态反例] 最终 source/target 包含保证缺失

尽管提示自身有预留，完整生产链仍没有最后不变量检查：

- capacity 在 `Bar.RenderLoop.cpp:8833-8842` 决定并创建资源；
- predicted viewport 主要在其后构造，source 在 `:9376-9389` 才计算；
- 之后没有验证 `candidateSource >= 0` 且 `candidateSource + candidateSize <= capacitySize`，也没有在越界时重新扩容/重算 source；
- `pptSrc + psize` 最终直接交给 ULW（`:12306-12327`）。

此外，capacity 的 dock 包络来自当前 `bottomDockElasticBaseBounds`，而 predicted viewport 可以包含主栏/居中动画 range 后再外扩（`:8926-8963,9336-9346`）。两者不是同一个完整预测输入，因此不能由“提示峰值也进了 capacity”推广为“所有最终 viewport 都在 target 内”。

**历史佐证：**09-22 的生产 helper 探针分别构造了负 source 和居中端点 `capacity.right=1573 < viewport.right=1753`（`.trellis/tasks/09-22-ui3-animation-stutter-investigation/research/geometry_probe.cpp:20-29,44-66` 及 `geometry-probe-results.txt`）。它证明 API 组合缺少包含保证，不是本次 GUI 已复现。

**现有测试缺口：**`TestReservedEnvelopeStaysInsideCapacity` 直接把 `reservedEnvelope` 设为 `Deflate(capacityBounds, padding)`（`InkeysHeadlessTests/window_geometry_tests.cpp:368-395`），前提已保证不越界，无法测试生产 predicted envelope 超过 capacity 的情况。

### 3.3 dirty 能否补救窗口不足，以及仍需分辨的情况

- `BarDirtyRegionTracker::Observe`、`IncludeDamage` 和最终 damage 都会被裁剪到 `BeginFrame(windowBounds)`（`Bar.DirtyRegion.h:38-59,85-89,115-133,232-240`）。随后 `presentDirty` 又与 candidate viewport 求交（`Bar.RenderLoop.cpp:9440`），ULW client dirty 再裁到 candidate 尺寸（`:12253-12266`）。**窗口/viewport 若缺顶部，dirty 无法把像素补回来。**
- 反过来，蓝框够大也不能证明 dirty 正确：D2D clip/Clear 与 ULW dirty 都依赖同一 `presentDirty`。必须把 actual indicator envelope、previous/current damage、candidateViewport、source 和 client dirty 同帧比较。
- 提示映射失效标记主要依赖弹簧/根布局变化（`:7293-7327`）。若未来有分支只改变 base geometry 而弹簧与进度均不变，仍需核对提示 key 是否失效；不能只看当前有 `MarkChanged` 就证明所有分支覆盖。普通拖动显示冻结、淡入淡出进度变化及 display 全脏会掩盖很多此类情况，所以目前只能列为条件性风险。
- 文案测量只使用普通 metrics（`:7691-7703`），没有单独的 glyph overhang 验证。如果实际问题是文字上缘而非矩形/描边，需另行读取真实 D2D 像素/字体 overhang；不能用矩形包络替代该结论。

### 3.4 裁剪根因分级

- **静态可确定：**提示本来就越过主栏顶边；窗口与 dirty 是独立但串联的裁剪点；最终 source/target 没有包含性兜底；对应测试未覆盖生产越界。
- **只属推断：**本次蓝框不足究竟由哪一项预测遗漏、缓存或坐标重解释造成；dirty 是否另有一笔遗漏；Windows 是否按 candidate 实际呈现。没有实际帧 RECT/像素前，不应把“扩大 dirty”或“扩大窗口”任何一项当作已证实修复。

## 4. 关键 Git 提交与 Trellis 历史设计证据

| 证据 | 设计演变与当前关系 |
|---|---|
| `eaedf9c5`（2026-08-15）及 `08-14-ui3-bar-bottom-dock-elastic` | 引入 20 DIP 吸附、纵向果冻、窗口直移/ULW 合同。原设计要求捕获窗口先到 dock、内容端点连续，及弹性阶段一次性预留窗口包络（任务 `design.md:20-37`）。这是设计起点，不代表现状。 |
| `5790b9dc`（2026-08-15） | 捕获下端点动画成为独立机制，解释当前 `captureBottom` 的来源。 |
| `08-20-ui3-bar-bottom-dock-feedback`、`ea7da650`、`9a7d1f12` | 引入当前 30 DIP 提示、DWrite 动态宽度、Back/光影包络和主栏果冻时的提示 dirty。PRD `R2` 明确要求首次激活完整峰值 damage（`prd.md:21-33`）；其手工视觉验收仍列为后续（`implement.md:77-79`）。 |
| `5a55106e`、`f7c9b5d5` | 居中果冻和成功远端重基准；后者明确 40 DIP 捕获带不能被旧 24 DIP 截断，并处理首次转换被后续采样赶超。当前水平 helper 延续该路线。 |
| `19ea2441` 与 `08-23-ui3-bar-drag-jitter-collapse-damage` | 强化释放交接、陈旧帧目的地、整窗替换和两轴命中。历史任务指出 viewport/source/size 与 dirty 必须同事务。 |
| `342990fe`（2026-09-09） | 修复旧帧冒充新 serial、写者所有权、捕获成功底边 seed、失败重试需求。其任务中的三帧机制与本次症状相似，但当前代码已包含该修复。 |
| `ed6012fc`（2026-09-10） | 临时追踪与额外几何修复。其实机报告证明代理抓点误差、跨模式速度/形状交接和吸收快照问题；`issue1-geometry-audit.md` 同时明确这些不能自动等同所有截图的唯一原因。 |
| `24efcde4`（2026-09-10） | 用户选择恢复 `342990fe` 基线，撤回 `ed6012fc` 的临时诊断和额外修复。`git diff 342990fe..24efcde4 --stat` 只剩 4 个任务文档，产品/测试/工程/spec 相同。故 `ed` 的修复方案不能直接当作当前实现，也不应未经新证据原样重新应用。 |
| `09-22-ui3-animation-stutter-investigation` | 记录 capacity/viewport 集成缺包含保证和真实几何探针；D3 后续设计明确要求“先完整 viewport/envelope，再确保容量”（`design.md:59-63`）。同时该任务明确长帧/退避是另一类问题，不应混入本次直接根因。 |
| `3e5bb005`（2026-10-19）与任务栏后续 | 引入实际任务栏线、底行保护及稳定 Y 动画；拖动/恢复/场景放置排除该 Y 写者。相关历史研究也强调吸附和恢复的几何不能被新任务栏动画重置。 |

相关 Trellis 文档：

- `.trellis/tasks/08-14-ui3-bar-bottom-dock-elastic/{prd,design,implement}.md`
- `.trellis/tasks/08-20-ui3-bar-bottom-dock-feedback/{prd,design,implement}.md`
- `git show 342990fe:.trellis/tasks/08-23-ui3-bar-drag-jitter-collapse-damage/{prd,design}.md`
- `git show ed6012fc:.trellis/tasks/08-23-ui3-bar-drag-jitter-collapse-damage/research/{issue1-geometry-audit,issue1-real-trace-geometry}.md`
- `.trellis/tasks/09-22-ui3-animation-stutter-investigation/{findings,design}.md` 及其 `research/geometry_probe.cpp`
- `.trellis/tasks/archive/2026-10/10-19-taskbar-dock-follow/research/bar-implementation.md`

## 5. 现有测试、复现能力与最小验证方法

### 5.1 现有测试能证明什么、不能证明什么

| 现有入口 | 覆盖价值 | 不能证明的事项 |
|---|---|---|
| `bar_bottom_dock_tests.cpp:64-148` | 居中左右展开、24..40 DIP、捕获/脱离/重捕获远端和容量 helper | 不是完整生产 RenderLoop/ULW 逐帧序列 |
| `:150-219` | 旧浮动帧不可确认捕获、失败候选重 seed、下一帧弹簧 | 不覆盖当前用户环境的全部交接和真实 OS 帧 |
| `:221-258` | 跨 recovery、origin、zoom、translation 的成功底边 seed | 只测捕获 helper，不覆盖脱离首帧积分 |
| `:395-485` | 首次转换被后续采样赶超、失败重试、陈旧帧 translation | 证明 barrier 模型，不是实际窗口截图 |
| `:1362-1390` | 固定 `e/c` 和 `windowShift=e*zoom` 时脱离公式连续 | 刻意排除了候选/成功差异、首帧 dt、取整和限幅，不能覆盖 2.2 |
| `:798-900` | 提示几何、缩放、Back/stroke/Gaussian/AA 包络和旧新 dirty | 不证明 capacity/viewport/source 在完整生产中包含包络 |
| `window_geometry_tests.cpp:21-103` | 简化的 resize/直移/失败锁与坐标不变量 | 使用手工 viewport/capacityOrigin 和模拟 ULW，不是实际协调器 |
| `window_geometry_tests.cpp:368-395` | 已位于容量内的预留包络可得到合法 source | 前提人工保证包含，无法发现生产越界 |
| `dirty_region_tests.cpp` | dirty 旧新并集、重试、映射变化、调试覆盖 | 不含提示首次 reveal 的完整窗口/D2D/ULW 链 |

父任务随后已用原生 ARM64 MSBuild 对当前 HEAD 构建完整 `InkeysRepo.sln Debug|ARM64`，结果 0 警告/0 错误且目标均 up-to-date；同一 `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` 产物退出 0（见 `.trellis/tasks/10-25-ui3-bar-centering-dock-geometry/research/shared-frame-contract-and-validation.md`）。**本研究代理未重新构建或运行测试；更重要的是，即使全绿也没有复现本问题的完整视觉场景。**

### 5.2 最小自动化回归（后续实现阶段）

1. **脱离首帧反例：**在 `bar_bottom_dock_tests.cpp` 扩展实际 tracker + spring + frame translation 的组合，不再只构造两个相同映射。至少覆盖：
   - 成功 `e0/c0` 与候选 `e1/c1` 不同；
   - `dt=0` 与 `dt=1/60`；
   - 失败/作废候选后脱离、快速再次捕获；
   - raw offset 超过 24 DIP、不同 zoom/原点/整数 translation。
   断言的是“扣除同一输入正常位移后的屏幕顶/底残差”，而非只检查 spring 最终收敛。
2. **吸收往返：**在成功快照吸收前后，对 base/visual 端点、主按钮中心、raw body center 和 source/destination 重建屏幕坐标，要求除真实 OS 移动外不变；并模拟下一次手势在完整呈现前启动。
3. **真实协调器离屏：**复用 `CreateBarOffscreenRenderCallback`（`Bar.RenderLoop.cpp:13142-13153`）和现有交互 worker 驱动实际 `Seek`，注入 present sink 复制当次 `UPDATELAYEREDWINDOWINFO` 指针指向的值。逐候选记录 mode/phase、serial、`e/c`、frame/presented translation、capacity origin/size、source/size/destination、presentDirty 和提交结果；不能只调用 helper 自证。
4. **提示全链：**在同一离屏链触发普通/居中提示首次 reveal、宽度双向切换、主栏果冻、失败重试和最终 idle。每帧断言：
   - `indicatorEnvelope ⊆ candidateViewport`；
   - `0<=source`、`source+size<=targetCapacity`；
   - 变化时 previous/current envelope 均被 present damage 覆盖；
   - destination/size 与当帧 translation 一致，失败不推进成功快照。
5. **测试必须先证明旧实现会失败。** 若只增加与现有 `CheckDetachContinuity` 或人工 Deflate 相同前提的绿测试，不算复现。

### 5.3 主会话/人工最小运行验证

1. 先用当前 HEAD 完整构建，再运行既有 `InkeysHeadlessTests --no-window` 作为回归底线，而不是问题复现。
2. 开启现有调试框，分别记录红/绿 damage 与蓝色 HWND；在有界、默认关闭的临时诊断中补充当前 raw capture 没有的 dock 字段：两轴 mode/phase、`e/c` 及速度、输入/帧/成功 serial、deferred barrier、candidate/presented translation、indicator envelope、candidate/source/client dirty。现有 `[UiPointerDrag]` 500 ms 摘要不足以证明单帧原因。
3. 同步保留异常前后至少 3 个**成功呈现帧**和被跳过候选，记录真实 `GetWindowRect`、ULW 参数和屏幕裁剪。软件“成功”不能替代实际 DWM 像素。
4. 最小操作矩阵：
   - 从上方、下方慢速进入吸附，再分别向上、向下退出；
   - 普通底栏与居中底栏；从中心左右进入/离开 40 DIP 带；横纵同样本切换；
   - 回弹未结束反向、释放后立即再次拖动、窗口/viewport 改变时连续切换；
   - 普通提示与居中提示双向切换、首次 Back 峰值和淡出；
   - 动画开关、既有 DPI/zoom、底部/侧边/自动隐藏任务栏及适用 PPT/白板场景。
5. 判因方式：
   - 若异常帧是 `SetWindowPos` 移旧位图，查 barrier/所有权；
   - 若 ULW destination 跳而内容映射连续，查窗口 tuple；
   - 若 destination 稳定而底边/抓点残差突变，查 2.2–2.4 的弹簧/快照；
   - 若 envelope 超 candidate 或 source，查窗口容量；
   - 若 candidate 完整但 D2D/ULW dirty 缺顶部，查 dirty；
   - 若软件矩形全正确而屏幕仍裁，转真实 ULW/DWM/驱动验证。

## 6. 候选修复边界与权衡（仅规划，未实施）

| 候选边界 | 应包含的内容 | 主要权衡/禁止事项 |
|---|---|---|
| 脱离首帧成功形状交接 | 为 `BottomDocked -> Floating` 增加与捕获对称的成功映射/端点重基准；首个成功候选不提前积分；按同一坐标语义处理速度；保留当前指针正常位移 | 会改变回弹第一步手感，必须 A/B 实测；不能只把 24 DIP 再放大、禁用弹簧或隐藏一帧 |
| 真实抓点与形状模型 | 若运行证据指向 2.3，按下时保存成功图像内归一化抓点，再由实际仿射求新旧形状；主按钮/Logo/主栏保持同一模型 | 改动面大于单个 seed，且 `ed6012fc` 类似扩展曾被用户回退；不得未经新证据原样恢复该大补丁 |
| 吸收快照一致性 | 吸收时同步平移两轴 base/visual 端点及相关中心/指示器数据；区分真实直移与布局吸收；成功后仍只发布完整 tuple | 需覆盖吸收后下一手势竞态、source/destination 和命中；不能通过移动真实 HWND 来“补偿”内部坐标 |
| 最终容量/映射不变量 | 在 `EnsureDeviceResources` 前以完整 predicted envelope 做最后包含检查；不足则扩容并重算 origin/source/mapping，必要时整窗替换；保留 idle 紧致 viewport | 可能增加 backing 面积、重建和整窗提交成本；不得只扩大 dirty 或无限保留高水位来掩盖，缩容须另以稳定几何证明 |
| 提示 dirty 完整性 | 继续复用统一视觉包络，逐分支验证 base geometry 改变是否使提示 key 失效；文字问题另测 overhang | 避免把所有视觉每帧全脏；窗口不足与 dirty 遗漏分别修，不互相替代 |
| 诊断与回归先行 | 先加入有界帧采样和能失败的脱离/裁剪回归，再选最小行为修改 | 不增加默认逐帧磁盘日志，不以关闭动画、吸附或动态宽度作为规避 |

**不建议直接改动的部分：**当前 serial CAS、deferred barrier 和普通捕获 seed 已对应历史缺陷。除非运行帧证明其仍被绕过，不应重写这些保护或回到旧 `SetWindowPos` 直移模式。

## 7. 仍需主会话运行确认的问题

1. 用户看到的异常帧究竟是旧位图被 `SetWindowPos` 直移、ULW destination 错、D2D 内容映射错，还是实际 DWM 呈现与软件提交不同？
2. 第一张异常帧前后的成功 `e/c`、候选 `e/c`、速度和 dt 是什么？脱离是否确有首帧积分或候选污染？
3. 异常前是否发生 release absorb、立即再次按下、失败/跳过候选、显示环境切换或 viewport/source/resize？
4. 普通捕获的底边 seed 是否使用了一张刚被吸收但端点未重基的快照？水平居中是否有同类 X 交错？
5. 用户所称“主栏底部到任务栏上方”对应的是名义 base bottom、真实描边底边还是 debug/HWND 底边？必须对齐同一可见边界。
6. 提示被裁的是矩形填充/描边、光影，还是文字 glyph？当帧 envelope、previous/current damage、candidateViewport、capacity/source、D2D clip 和 ULW client dirty 各是多少？
7. 蓝框是否真的小于实际绘制包络，还是蓝框完整而红框/脏区截断？真实 HWND 是否与 candidate 同步？
8. 主会话已确认当前 HEAD 完整 ARM64 Debug Solution 构建为 0 警告/0 错误、既有 `--no-window` 退出 0；后续问题是新回归能否先在该基线稳定失败，再证明修复有效？

### Related Specs

- `.trellis/spec/native-desktop/index.md:5,48-60`：生产 UI3 唯一路线、共享渲染与测试边界。
- `.trellis/spec/native-desktop/rendering-and-ui.md:603-850`：dirty/capacity/viewport/成功事务；`:871-1031`：二维吸附、捕获 seed、提示和验证矩阵。
- `.trellis/spec/native-desktop/ui3-render-diagnostics.md:30-47,95-103`：raw capture 约束及无窗口/真实 ULW 的证据分界。
- `.trellis/tasks/10-25-ui3-bar-centering-dock-geometry/research/shared-frame-contract-and-validation.md`：父任务共享不变量与基线结果。

### External References

- 未使用外部网站或库文档；本问题可由仓库实现、Windows API 调用点和本地历史直接调查。ULW/DWM 的最终视觉行为仍明确要求实机验证，不能用 API 名称推断。

## Caveats / Not Found

- **未找到能在静态代码中唯一解释所有报告帧的单一根因。** 本文给出的是当前可证缺陷、理想路径反证和可执行的区分方法；不将其包装成已完成的运行根因证明。
- 未重新构建、未运行测试、未启动 GUI/Office/WPS，未修改产品代码、既有测试、spec 或工程，未启动任务、未提交。历史实机结果只用于解释设计演变。
- 动态宽度与场景竖直移动时的居中缺陷属于 `10-25-ui3-centered-bar-dynamic-centering`，本文只在共享 translation/快照/窗口合同处交叉引用，不将其根因搬到本任务。

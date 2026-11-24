# Draw3 脏区现状（2026-11-23）

基线 `refactoring/draw@cbcccf9a`。只读调查，未改代码、未编译、未运行。行号随后续提交会漂移，开工前须复核。路径省略前缀 `Inkeys/Inkeys/Drawing/Draw3/`。

标注：**[读]** 直接读到的代码；**[推断]** 由代码推出、未运行验证。主体由只读子代理逐条读码得出，主 Agent 抽查了帧脏区声明、`DirtyRectsCount`、`prcDirty`、两处多矩形容量常量与配置字段，均一致。

## 0. 结论

每帧的脏区是单个 `RECT frameDirty`。所有来源（含多指的每个接触）都用 `UnionRectInPlace` 并成一个包围盒；上屏合成只做一次并覆盖这个矩形；交换链路径 `DirtyRectsCount` 恒为 1；ULW 每次提交只传一个 `prcDirty`。

仓库里已有两处多矩形结构：L0/L1 写入登记（最多 8 个）和粒子脏区登记（最多 512 个）。两者都不参与上屏合成与呈现，粒子那份在取快照时又并回一个包围盒。

## 1. 帧脏区的结构与来源

**结构 [读]**

- `Draw3.DrawingController.cpp:24098` `RECT frameDirty = {};`，每轮帧循环新建。
- 并入函数 `UnionRectInPlace`（`Draw3.StrokeGeometry.cpp:2092-2104`），min/max 包围盒，空矩形忽略。
- 收尾：`:25448` 裁剪到画布；`:25450-25451` `forceFullPresent` 时改为整屏。
- 每个接触另有累计矩形 `runtime->visibleDirty`（单个 RECT，`:24647`、`:24889-24891`，复位 `:25346`）。

**并入来源（全部是包围盒合并）[读]**（行号均为 `Draw3.DrawingController.cpp`）

| 来源 | 位置 |
| --- | --- |
| 形状 UI 上下文校验 | `:24099` |
| 视口恢复 tile | `:24100`（来源 `:23915`、整屏 `:23624`） |
| 激光烘干待消费区 | `:24101`、`:24536` |
| 粒子设置变化 | `:24103`、`:24975` |
| 粒子包络（帧初，上一帧与本帧） | `:24114-24115` |
| 激光透明度变化或过期 | `:24125-24126`、`:24522-24523`（`laserStableBounds` + `laserLiveBounds`） |
| 空闲时的画布命令（撤销、重做、翻页、清屏等） | `:24131`、`:25385`；翻页取整屏 `:21110` |
| 画布调试板 | `:24177` |
| 手掌切换重建 | `:24599`、`:24602` |
| 活动形状（L0 旧、新） | `:24645-24646` |
| 激光增量 coverage | `:24704`；回退 `:24717`；非增量 `:24736` |
| 笔、荧光笔、橡皮的 L1 稳定段与 L0 实时段 | `:24886-24888`（`stableDirty`、`lastL0Rect`、`currentL0Rect`） |
| 激光批次烘干 | `:24915`、`:24919`、`:24924` |
| Compute 不可用回退 | `:24977` |
| 清屏碎裂 | 每帧 `:25017`；结束帧并入整屏 `:25015` |
| 形状控制屏障 | `:25032` |
| 抬笔结束的接触 | `:25094`（激光）、`:25100`（形状构造步骤）、`:25309`（整笔累计 `visibleDirty`） |
| 抬笔写入 L2 | `:25259` |
| 共享层重建、焦点层 | `:25317`、`:25319` |
| 形状构造预览 | `:25380` |
| 粒子包络（帧末） | `:25403-25404` |
| 激光笔尖 | 新旧 `:25417-25418`；只要 `frameDirty` 非空就并入当前笔尖 `:25420-25423` |
| 激光生命周期动画 | `:25426-25427` |
| 光标 | 新旧 `:25432-25433`；只要 `frameDirty` 非空就并入当前光标 `:25435-25439` |
| 调试标记 | `:25443` |

**强制整屏 [读]**：`forceFullPresent = true` 出现在 `:17318, 21124, 22169, 22471, 22706, 23124, 23558, 23623, 23732, 23783, 23804, 23873（视口刷新、平移）, 24163`。

**现存多矩形结构 [读]**

- L0/L1 写入登记 `OperatorContentTracking`（`Draw3.Renderer.cppm:518-524`），容量 `kOperatorContentRectCapacity = 8`（`:35`）。合并规则（`Draw3.Renderer.cpp:310-368`）：相交或相距不超过 4 像素就合并，直到不再变化；满了并入“合并后面积增量最小”的那个。只用于清除：`ClearOperatorLayerContent`（`:423-475`）按多矩形 `ClearView`，面积之和超过画布 1/16 退回整屏清。
- 粒子脏区登记 `LaserParticleDirtyTracker`（`Draw3.LaserParticles.cppm:360-380`），每个发射批次一个带到期时间的区域，容量 512（`:22`）；槽满合并最早到期的槽（`Draw3.LaserParticles.cpp:698-708`）；`Snapshot()` 输出单个 `activeBounds`（`:741-742`）。

## 2. 多指

- **[读]** `for (RuntimeStroke* runtime : active)`（`:24609-24895`）里每个接触的三个矩形直接并进同一个 `frameDirty`。
- **[读]** `RebuildActiveLayers`（`:6565` 起）把所有活动接触并成一个 `dirty` 再并入。
- **[读]** 抬笔时各接触的整笔累计矩形也并进同一个 RECT。
- 结论：没有按接触分开的合成或呈现矩形。两指分在屏幕对角时外接矩形接近全屏（与 `11-17-win7-ulw-presentation/research/hover-cursor-layer-20261117.md` §2 一致）。

## 3. 上屏合成怎样使用脏区

**调用链 [读]**

- `:25498` 仅在 `frameDirty` 非空且栅格提交未失败时合成。
- 普通帧 `CompositeLayersToBackBuffer(RECT dirty, bool orderLiveOverStable)`（`:16723-16737`，调用 `:25550`）：
  1. `renderer_.CopyResource(backBuffer, layerL2, dirty)`，即一次 `CopySubresourceRegion`（`Draw3.Renderer.cpp:139-149`）；
  2. `ApplyActiveOperatorLayers(..., dirty, ...)`（`:6552-6563`）：两层都空跳过，一层有内容走类型 25，两层都有走双层；
  3. `DrawDebugPlateUnder(dirty)`；
  4. `DrawWhiteboardPatternUnder(dirty)`。
- 平移帧走 `CompositeTrustedL2SnapshotToBackBuffer` 再接 `ApplyActiveOperatorLayers`（`:25533-25548`）。
- 之后的叠加（`:25556-25602`）：碎裂快照（与 `frameDirty` 求交）→ 粒子 → 点击光波 → 激光已烘颜色（`laserStableBounds ∩ frameDirty`）→ 激光笔画层 → 前景粒子 → 激光笔尖 → 光标 → 调试标记。
- `PresentFrame(frameDirty, forceFullPresent, ...)`（`:25622`），注释写明一帧最多一次 backbuffer 合成和一次 Present。

**`InkRenderer::ResolveOperatorLayers(dstRTV, stableLayer, liveLayer, RECT rect, float shapeType)`（`Draw3.Renderer.cpp:164-226`）[读]**

- 几何：rect 的两个角点写进 `compositeVertexBuffer`，`Draw(6, 0)` 画一个覆盖 rect 的四边形。不是全屏三角形，不用 scissor，viewport 保持全屏。
- 每次调用的固定开销：两次 `Map(WRITE_DISCARD)`（顶点缓冲、`globalCB`）；`OMSetRenderTargets`（及可选深度状态）；输入装配两项；VS 着色器、SRV、常量缓冲；PS 着色器、常量缓冲、5 个 SRV、采样器；混合与光栅状态；`Draw(6)`；画完解绑 VS SRV、5 个 PS SRV 与采样器。

**其他 pass [读]**

- 白板图案（`Draw3.RendererPattern.cpp:62-109`）：CPU 枚举 dirty 内的图元，再用 scissor 硬约束，因为 under 混合不幂等。
- 激光矩形 pass（`Draw3.RendererLaser.cpp:309-376`）：按矩形设 scissor 后 `Draw(6)`。
- 粒子（`:599-611`）、光波（`:558-597`）、笔尖（`:517-556`）、光标：直接画到 backbuffer，不设 scissor，不按 dirty 裁剪。
- **[推断]** 这几类靠“自身包络已并入 `frameDirty`”保证正确。“只要 `frameDirty` 非空就并入当前光标/笔尖”也是因为底图重合成会盖掉它们，必须重画。

## 4. 呈现

**接口 [读]**：`TransparentPresentationController::Present(RECT dirty, bool presentFull, uint64_t contentRevision = 0)`（`Draw3.TransparentPresentation.cppm:175`，实现 `.cpp:2532-2630`），只接收一个矩形。

**交换链路径（DComp、Win7Dwm）[读]**：`PresentSwapChain(RECT dirty, bool presentFull)`（`.cpp:2150-2189`）。非整帧时 `DirtyRectsCount = 1`（`:2161`）；整帧时参数全零；`Present1(0, 0, &parameters)`（`:2179`）。

**ULW 路径 [读]**

- `UlwDirtyRectPresenter`（`.cpp:475`）：双 Staging 槽异步读回，仍在绘制线程内，没有独立提交线程。
- 同步 `Present`（`:1052-1068`）：`IssueCopy`（`:903-911`）一次 `CopySubresourceRegion` 复制 dirty 盒；`CompleteCommit`（`:928-1049`）`Map` 后 `CopyAndInspectUlwDirtyRows`（`:406-473`）只复制 dirty 各行到持久 DIB，再 `UpdateLayeredWindowIndirect`，`prcDirty` 为该矩形或整帧时为空（`:1021`）。
- 异步 `PresentAsync`（`:1156-1209`）：与累计脏区 `accumulatedDirty`（单个 RECT）合并（`:1179`）；本帧不提交时只累计（`:1189-1197`）；提交走 `IssueCommit`（`:1120-1152`），上一提交未完成时先阻塞完成它。
- 一次 Present 里可能完成两次提交（背压加本帧，注释 `:989`），帧首 `ServiceFrameStart`（`:1213`）、帧尾 `ServiceFrameEnd`（`:1230`）、空闲前 `CompleteBeforeIdle`（`:1251`）也会完成提交。每次 `UpdateLayeredWindowIndirect` 都只有一个矩形。

**与 DWM 合成节拍对齐 [读]**

- ULW：`RefreshUlwCompositionState`（`:1531-1571`）读 `DwmIsCompositionEnabled` 与 `DwmGetCompositionTimingInfo`；`MakeUlwTiming`（`:1574-1602`）；提交与否由 `UlwCommitPlanner`、`UlwReadbackPacing`（`Draw3.UlwReadbackPacing.h`）决定；相位调整 `:2860-2862`。
- DComp 用 `GetFrameStatistics`，Win7Dwm 用 `DwmGetCompositionTimingInfo`（`:2817-2858`）；waitable 等待 `WaitFrameLatency`（`:1619`）。
- 选项：`TransparentPresentationOptions` 的 `enableWaitableWait`、`enableUlwAsyncReadback`、`enableUlwCompositionAlign`、`enableDcompCompositionAlign`、`enableWin7DwmCompositionAlign`（`.cppm:89-96`）。

## 5. 激光、粒子、荧光笔、橡皮

- **激光主体 [读]**：不进 L0/L1/L2，有独立纹理，合成时直接画到 backbuffer。书写中是增量脏区；抬笔后留存与淡出期间每帧并入 `laserStableBounds` + `laserLiveBounds`，即整组轨迹的包围盒。
- **画布粒子 [读]**：每批出生时登记保守包络（最大行程 + 2×半径 + 2 像素，`Draw3.LaserParticles.cpp:559-576`）。活跃期间每帧并入所有活跃批次的并集和上一帧的并集。**[推断]** 包络是整个寿命的最坏覆盖，不随粒子消亡收缩，批次到期才变小。
- **点击光波 [读]**：登记整个寿命的最大包络（`:19925-19929`），与粒子共用登记。
- **清屏碎裂 [读]**：每帧并入被清墨迹的包围盒；碎完那一帧并入整屏。
- **荧光笔 [读]**：L1 稳定段脏区是本次稳定点列的几何 bounds，L0 脏区是未提交真实点加预测点的几何 bounds；帧内三者并入。抬笔后每条 Stroke 独立 resolve 到 L2（`:25251-25256`）。
- **橡皮 [读]**：只写 L1。每帧脏区是从上次提交点到最新真实点这一段的包围盒（外扩 3 像素，`Draw3.StrokeGeometry.cpp:2125-2169`、`:2798-2818`）；速擦光标另经 `cursorVisualBounds` 并入。**[推断]** 大橡皮每帧脏区约为“本帧移动距离 + 直径 + 6 像素”。

## 6. 性能页与布尔选项链路

- 配置：`Inkeys/Inkeys/Other/Other.Config.cppm:627-647`，`Experimental.Inkeys3.Draw3` 组。
- 设置页：`Inkeys/Inkeys/UI/Setting/Setting.cpp`，性能页 `:4762-4965`，分节：帧率 `:4797`、渲染 `:4819`、高级 `:4889`、底层 `:4901`。
- 文案：`Inkeys/src/i18n/*.jsonc`（20 种语言），键路径 `SettingsUI.Performance.*`。

以 `EnableUlwAsyncReadback`（默认开、重启生效）为例的完整链路 [读]：

1. `Other.Config.cppm:640` 配置字段。
2. `Setting.cpp:2400` 设置页本地镜像。
3. `Setting.cpp:4958-4963` `Design::ToggleRow(...)`，切换后写回并 `QueueConfigWrite()`。
4. `src/i18n/zh-CN.jsonc:1143-1146`。
5. `Inkeys/IdtMain.cpp:3996-3997` 启动时取值。
6. `Draw3.Host.h:318` → `Draw3.Host.cpp:2361`。
7. `Draw3.TransparentPresentation.cppm:91` → `.cpp:1502` `UlwAsyncActive()`。

即时生效的写法可参考 `UnthrottledMouseMoveWhileDrawing`（`Setting.cpp:4890-4899`，调用 `SyncDraw3State()`）。

## 7. 矩形工具

没有独立的矩形工具模块 [读]。同类函数散落在：`Draw3.StrokeGeometry.cpp:2092-2123`（`UnionRectInPlace`、`IsEmptyRect`、`ClampRectToCanvas`、`GetFullCanvasRect`）、`Draw3.TransparentPresentation.cpp:252-269`（`IsEmptyUlwRect`、`ClipUlwRect`、`UnionUlwRect`）、`Draw3.Renderer.cpp:329-337`（局部 `area`、`unite`）、`Draw3.LaserParticles.cpp:69`、`Draw3.InkHistoryGpu.cpp:71`、`Draw3.CanvasDebug.cpp:55`。求交直接用 Win32 `IntersectRect`。

## 8. 诊断

性能看板与设备性能账本没有脏区面积或像素统计 [读]。现有相关数据：Host 快照 `lastDirtyRect`（`Draw3.Host.h:435`）与 `ulwDirtyRectPresentCount`（`:396`），仅供隐藏窗口测试；ULW 每秒汇总日志 `MaybeLogPipelineSummary`（`Draw3.TransparentPresentation.cpp:1275-1303`）有 `rowsAvgMs`、`ulwAvgMs` 等耗时但没有面积；`OperatorLayerCounters`（`Draw3.Renderer.cppm:39-51`）只供自检；`RuntimeMetrics` 有合成阶段耗时但没有面积。

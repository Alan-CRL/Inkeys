# Research: 白板点阵/网格背景纹理的归属（跟随画布平移）

- **Query**: 决定白板点阵/网格背景纹理应画在哪里才能随画布平移（重绘点/线位置，不滚动位图，允许微小不同步帧）；比较“逐帧更新 Freeze 窗口”与“并入 Draw3 合成路径”，给出换页/换主题握手、需移除/修改的 Freeze/UI 路径、相关测试，并给出推荐与最小变更集。
- **Scope**: internal（代码 + `.trellis/spec` + 归档任务），只读
- **Date**: 2026-11-06
- **注意**: 本文所有耗时/速率均为由代码推导或类比，**未做任何实测**；真机 GUI、Win7、ARM64 硬件/WARP 帧耗时均未验证。

## 0. 结论摘要

1. **逐帧更新 Freeze 窗口不可行**：每次更新是“整屏 D2D + GetDC(COPY) + ULW + candidate/receipt”，单次至少占 2 个 UI3 批次（60FPS 调度 → 上限约 30 次/s，owner 忙时更低），与 Draw3 平移帧（目标 120Hz 量级）差 3–4 倍；视口也没有现成通道。1000+ DIP/s 的平移下，33–50ms 滞后相当于 100–160 px 错位，不属于“微小不同步”。
2. **并入 Draw3 合成路径可行且结构上最贴合**：在 `DrawDebugPlateUnder` 同位置（所有墨迹/快照/橡皮之后）追加一个 *under* pass，只写 backbuffer，不进入 L2/历史/热前像/快照；平移帧本来就是全屏合成 + 全屏 Present，所以图案不新增脏区；与墨迹共用同一帧同一 viewport，**零延迟**。预乘 alpha 下 18% 图案 under 在墨迹之下，再由 DWM over Freeze 底色，数学上与今天相同（over 结合律），仅有 ≤1 LSB 量化差。
3. **换页/换主题**：只有“底色 + 图案 + 墨迹全部在 Draw3 同一帧”才完全原子；“只迁图案、底色仍由 Freeze 提供”能让图案与墨迹原子，但底色仍经 Draw3→Host→state 线程→UI3→owner 异步（≥50ms，与今天墨迹 vs 底色的关系相同）。
4. **推荐**：图案迁入 Draw3（先用现有 shape 16 的 primitive + 新 under blend + scissor，不改 HLSL），底色先留在 Freeze；若验收要求主题/翻页严格原子，再在同一 pass 加“底色 under”。不做 Freeze 逐帧更新，不加 viewport 桥接通道。

## 1. 逐帧更新 Freeze 窗口：成本与可行性

### 1.1 一次 Freeze 更新的真实流程（`Inkeys/Inkeys/UI/Whiteboard/Whiteboard.cpp`）

| 步骤 | 位置 | 说明 |
|---|---|---|
| 目标 | `:79-90`, `:306-335` | 完整主屏 `rcMonitor` 的 D2D target（4K BGRA8 ≈ 33MB） |
| 绘制 | `:398-402` | `BeginFrameDraw(damage)` → `backgroundScene.Render` → `FinishFrameDrawCommands` |
| 同步 | `:404-405` | `gdi->GetDC(D2D1_DC_INITIALIZE_MODE_COPY)`，`GetDC` 无脏区参数（归档 `archive/2026-08/08-11-bar-window-dirty-present/prd.md:15,22`：“整个 D2D 目标的同步成本”） |
| 提交 | `:409-431` | `UpdateLayeredWindowIndirect`，经 `SubmitFreezeSurface`（`Inkeys/IdtFreezeFrame.cpp:84-91`，`freezeSurfaceMutex` 串行） |
| 登记 | `:456-460` | 建 `backgroundSubmission` → `PublishBackgroundCandidate` → **`return FrameResult::Retry`** |
| 回执 | `:341-378` | 下一批次：读 `TryReadCandidateReceipt`，`Queued/Executing` 或取不到租约 → `Retry`；全匹配后 `Prepare` / `CommitPresented` / `ConsumeDamage` → `Idle/Continue` |

- 在途提交期间**不画新像素**（`:55-56` 注释、`:341` 分支）。因此一个“像素帧”最少占 2 个批次（画+ULW+发布候选 / 读回执），回执返回 Idle 后下一次画最早在第 3 个批次。
- 调度器：共享 UI3 线程，`Retry`/`Continue` 都只是进入下一批（`UI/RenderPipeline/RenderPipeline.cpp:347-350`），批间隔 = 1/frameRate，默认 60（`:608`，可切 30：`:621`；等待逻辑 `:744-755`）。所有客户端回调串行执行（`:825-879`），Freeze 的 4K 重绘会与 Bar/PageControl 争用同一线程预算。
- 推导上限：60FPS 调度时 ≈ 30 次/s（回执在下一批返回的理想情形），30FPS 调度 ≈ 15 次/s；owner 忙则更低。Draw3 平移帧按 `target_fps` 节拍（`Draw3.DrawingController.cpp:18572-18582` 的 `WaitForFrameDeadline`；无效档位回落 120：`:7531`）。
- 视觉后果（计算值）：`runtime-and-rendering.md` 的 Good 例为 3200 DIP/s 甩动；33ms ≈ 106 px、50ms ≈ 160 px。

### 1.2 单次成本与协议冲突

- 平移时每个点/线位置都变，damage 必为全屏，ULW `prcDirty` 无法缩小；D2D 侧点阵约 14.6k 次 `FillEllipse`（4K、100% DPI，`Whiteboard.Theme.h:29-33`），网格约 250 条线。
- 协议语义冲突：
  - 若把视口当成“内容失效”→ `Invalidate()`（`Whiteboard.BackgroundState.h:106`）+ `CancelBackgroundCandidate()`（`Whiteboard.cpp:169-173`）每帧取消在途回执，连续平移时可能永远追不上；
  - 若不当成失效 → 需要改协议让多次像素提交共享/续用候选。规范明确“主题、DPI、设备、请求改变退休旧像素候选；新几何/身份等待 ACK 时不接纳新输入”（`rendering-and-ui.md:2641-2646`）。该协议为 workspace Prepare/Complete 设计，不是动画通道。

### 1.3 视口如何到达 Freeze（现状：没有通道）

- `HostRuntimeSnapshot` 没有 viewport（`Draw3.Host.h:321-385`；`Draw3.Host.*` 全文无 `viewport`）。
- 现有白板样式通道只带“页/主题/加载”：
  - `WhiteboardStyleToken {hostGeneration, generation, page, backgroundRgb, serial}`（`Draw3.Bridge.h:230-238`）——**含 RGB 不含 pattern，更不含 viewport**；
  - 发布点 `Host::Impl::ObserveWhiteboard`（`Draw3.Host.cpp:1230-1258`，token 在 `:1237`）由控制器 `publishWhiteboard`（`Draw3.DrawingController.cpp:10855-10868`）触发，仅页/主题/加载变化时调用；
  - → `PublishRuntimeRevision`（`Draw3.Host.cpp:809`）→ state 线程 `WaitForProductRuntimeRevision(...,250ms)`（`IdtState.cpp:3799`）→ `AcceptWhiteboardStyle` + `Whiteboard::PublishPageSnapshot`（`IdtState.cpp:3875-3880`；`Whiteboard.cpp:680-702`）→ `RenderPipeline::Request`（`Whiteboard.cpp:701`）。
- 逐帧视口要新增：控制器 observer 回调 → Host 原子/seqlock → Snapshot 字段 → state 线程唤醒 → `Whiteboard::PublishViewport` → UI3 请求 → Window owner 回执。跨 Draw3 绘制线程 / state 线程 / UI3 线程 / Window owner 四个线程，每跳增加抖动。

### 1.4 附带观察（与方案无关，影响“拖动”态平移节拍）

- 选择（拖动）态 Draw3 释放 1ms 系统 timer period（`Draw3.TimerPeriod.h:30-43`，`Draw3.DrawingController.cpp:16534`），平移帧等待粒度可能变粗；需实测。
- 白板选择态仍走 Primary 输出（`Draw3.Bridge.h:187-191`：`SelectionUsesAuxiliaryOutput` 对 Whiteboard 恒 false），且 `hideDrawpad` 对白板为 false（`IdtState.cpp:3941-3943`）→ 拖动态 Draw3 仍会 Present。

**结论**：Freeze 逐帧更新在速率、协议、线程跳数上都不成立。

## 2. 在 Draw3 合成/呈现路径内绘制图案

### 2.1 现有合成顺序与插入点

主循环平移分支与普通分支（`Draw3.DrawingController.cpp:18313-18357`）：

```
[平移且 L2 未清晰] CompositeTrustedL2SnapshotToBackBuffer   (:18345)  CopyResource(L2->BB) + 快照 under
ApplyOperatorLayers(backBuffer, L1, L0)                      (:18353)  Result = Add + Retain*Dest
DrawDebugPlateUnder(frameDirty)                              (:18355)  under blend, 仅 dirty∩板子
(非平移)  CompositeLayersToBackBuffer                        (:18357 -> :10377-10390，同序列)
之后: 激光粒子/激光主体/笔尖/应用光标/调试标记               (:18363-18395)
PresentFrame(frameDirty, forceFullPresent)                   (:18410)
```

- `CompositeLayersToBackBuffer` 共 3 个调用点：`:10464`（清屏合成）、`:10506`（`PresentFullCanvas`，resize 经 `:10558`）、`:18357`；平移分支在 `:18343-18356` 复制了同一序列 → 两处都要插入，宜合并为一个 helper。
- **现成先例**：`InkRenderer::DrawDebugPlateUnder`（`Draw3.RendererDebugOverlay.cpp:232-244`）与 under blend（`:115-125`，`SrcBlend=INV_DEST_ALPHA, DestBlend=ONE`）、快照 under blend（`Draw3.RendererSnapshot.cpp:98-108`）。调试覆盖层还有 `DrawDebugMarkerShapes`（`:260-268`）已把 `DrawShapePrimitives` 直接画到 backbuffer。
- **必须在 `ApplyOperatorLayers` 之后、且是 under**：L0/L1/橡皮的 `Result = Add + Retain*Dest`（`Draw3.Renderer.cpp:145-199`，`spec/shaders/cpu-gpu-contracts.md:199-218`）会把已在 dest 里的图案一并乘掉（橡皮 Retain<1 会在图案上挖洞）；快照 under 用 `INV_DEST_ALPHA`（`RendererSnapshot.cpp:98-108`），图案先入会遮蔽快照。
- **under blend 非幂等**：`dest' = dest + src*(1-destA)`；同像素画两次 alpha 会 0.18→0.33。所以 pass 只能写本帧刚从 L2 重新拷贝的矩形（= `frameDirty`），必须 scissor 或用目标矩形四边形。（`DrawDebugPlateUnder` 就是把 target 钳到 `dirty∩板`，`:235-243`。）

### 2.2 脏区 / 部分呈现 / `viewportRefreshPending` / `forceFullPresent`

- 平移每个视口增量都设 `viewportRefreshPending = true`（`:13835-13839`），下一步 `:16806-16861` 清 L2、重建 tile 计划并 **`forceFullPresent = true`（`:16858`）**；`frameDirty` 在 `:18268-18271` 被替换为整画布 → **平移帧本来就是全屏合成 + 全屏 Present**（DComp：`Present1` 不带 dirty，`Draw3.TransparentPresentation.cpp:1144-1158`；ULW：整屏读回 `:501-548`）。图案 pass 在平移帧不新增脏区/读回，只多一个 pass。
- 非平移帧（落笔、擦除、激光等）：pass 限制在 `frameDirty`，成本随笔迹面积；光标/激光/粒子区域会先并入 `frameDirty` 重新合成（`:18216-18262`），图案在该区被重铺而不叠加。
- 需要新增 `forceFullPresent` 的触发（目前都只刷新 Freeze）：
  - 主题/纹理命令 `SetWhiteboardTheme`（`:16016-16022`，仅 `publishWhiteboard()`）；
  - 进入白板：`SetWorkspace` → `restoreAfterDocumentSlotSwitch`（`:16024-16080`, `:14652-14688`）已经经 `resetGpuForPageSwitch` 置 `forceFullPresent`（`:14643`）；换页 `:16460` 同；resize 经 `PresentFullCanvas`（`:10558`）；DPI 变化。
- 状态可达性：`activeWorkspace`（`:10584`）与 `whiteboardThemes`（`:10611`）是 `Run()` 局部变量，而 `CompositeLayersToBackBuffer` 是成员函数 → 需把“当前页主题/viewport/dpiScale/是否启用”放到成员或传参。viewport 读取方式与 `:18318-18323` 的 `snapshotCanvas` 同源：`document_->PageAt(currentPageIndex_)->FindCanvas(kDefaultDeviceKey)->Viewport()`，与同帧 L2 tile 恢复使用同一 viewport（`:13835` → `:16838`）。
- viewport 单位实为 backbuffer 像素（名字叫 DIP）：`ScreenToCanvas = screen + viewport`（`Draw3.CanvasNavigation.cpp:139-151`），快照重投影直接把 viewport 与 `viewportWidth`（px）运算（`RendererSnapshot.cpp:203-216`）。范围 ±1048576，float 精度不足，建议 CPU 用 double 求 `fmod(·, step)` 再传 GPU；`step = 24 * dpiScale` px。

### 2.3 与各层/特性的交互（会不会漏进 L2/历史/导出/快照）

| 项 | 结论与证据 |
|---|---|
| L2 | 图案只写 backbuffer。L2 的写入点只有清屏/tile 恢复/笔画落定（`:16818`, `:15346`, `:18077`, `Draw3.InkHistoryGpu.cpp:727,1249,1313`）。全库 `backBufferTexture` 仅出现在创建、`CopyResource` 目标、Present/读回与测试，从不作为数据源。 |
| L1/L0 | `ApplyOperatorLayers` 先于图案，不改变其“Add + Retain*Below”数学。 |
| 可信 L2 快照 | `RefreshTrustedL2Snapshot` 复制的是 `layerL2Texture`（`RendererSnapshot.cpp:170`）→ 无图案；重投影/模糊只作用墨迹，图案在当前视口相位清晰绘制，快照未覆盖的暴露区也有图案；spec 要求“兜底像素不得写回 L2/文档/history/热前像/cache”（`runtime-and-rendering.md:887`）天然满足。 |
| 热前像 | 由当前 L2 捕获（`runtime-and-rendering.md:1023`），视口变化即 `DiscardHotPreimages`（`:13840`）→ 不含图案。 |
| composition cache | Canvas-local operator tile（`runtime-and-rendering.md:883-887`）→ 不含图案。 |
| 橡皮 | 作用于 L2/L1/L0 的 Retain；擦后透出下层图案，等价于今天透出 Freeze 上的图案。速度橡皮/hover 视觉在 composite 之后绘制。 |
| 激光/粒子/光标 | composite 之后 src-over（`:18363-18395`）；其 Retain*Dest 作用在含图案的 dest 上，与今天“激光画在 Freeze+图案之上”结果一致（over 结合律）。 |
| 保存/导出 | UInk 只存 CPU strokes + viewport（`captureCanvasTail`，`:10878-10910`）+ 主题 `rgb/pattern` JSON（`:10925-10926`）；Draw3 内没有基于 backbuffer 的导出/缩略图；白板手动导出不在范围（`whiteboard.md:33`）。**将来**做导出必须从 CPU 内容+主题另绘背景，不能指望 backbuffer。 |
| 桌面/PPT | pass 必须按 `activeWorkspace == Whiteboard && pattern != Blank` 门控，否则破坏“画布空时真透明”（`:10453`, `:10458` 注释；SelectionUlw 全帧零 alpha 校验 `TransparentPresentation.cpp:143-247`, `DrawingController.cpp:18475-18485`；`runtime-and-rendering.md:705`）。 |

### 2.4 预乘 alpha：18% 图案 over Freeze 纯色是否与今天一致

- 管线是预乘：backbuffer BGRA8，DComp 交换链 `DXGI_ALPHA_MODE_PREMULTIPLIED`、FLIP_SEQUENTIAL 双缓冲（`TransparentPresentation.cpp:1020-1023`）；ULW 用 `AC_SRC_ALPHA` 预乘 DIB（`:611`, `:624`），读回时校验 `pixel[c] <= alpha`（`:238-239`）。
- 今天：Freeze 为不透明底色 F，D2D 在其上叠 18% 黑/白图案，Drawpad 墨迹在最上。DWM 结果 = `ink ⊕ (pattern ⊕ F)`。
- 迁移后：Draw3 backbuffer = `ink + (1-ink.a)·P`（P = 图案预乘，under），DWM 再 over F：`ink + (1-ink.a)(P + (1-P.a)F)` = `ink ⊕ (pattern ⊕ F)`。over 结合律 → **数学恒等**；差异仅在 8bit 量化（`P.a = round(0.18·255) = 46`→0.1804，DWM 取整）≤1 LSB/通道。
- 视觉细节需像素对比：
  - D2D 线宽 1 DIP 居中于整数 px（相位 = 屏幕中心 `Whiteboard.Theme.h:26-28`，常见缩放下 `24·scale` 为整数），覆盖相邻两列各 50%；shape 16 的 PS 抗锯齿 `aaWidth = 1.5·fwidth(d)`、`smoothstep`（`inkPixelShader.hlsl:542-543`）在该情形同样得 0.5/0.5；圆点边缘柔和度略有差异。
  - 深/浅图案色已有同一亮度判据：`IsLightWhiteboardBackground`（`Draw3.WhiteboardModel.h:6-9`）与 `Whiteboard.Theme.h:21-22` 相同（>128000 → 黑，否则白）。
  - 相位：Draw3 与 Freeze 同尺寸（白板时 Freeze/DrawpadPresentation/Drawpad 都 `SetWindowPos` 到主屏 bounds，`Window.cpp:3174-3175`, `:3206-3217`）→ `phase = (W/2 − viewport.x) mod step`，viewport=(0,0) 时与今天一致。
  - ULW 回退路径对主画布加 1/255 命中底层 alpha（`TransparentPresentation.cpp:189-193`），对图案可忽略。

### 2.5 实现方案对比

| 方案 | 做法 | 优点 | 缺点/风险 |
|---|---|---|---|
| **R1（推荐起步）** | 复用 shape 16：点 = 零长度 SolidLine（半线宽圆点，`cpu-gpu-contracts.md` Geometry 节），网格 = 贯穿屏幕的长 SolidLine；经 `DrawShapePrimitives(..., blendStateOverride)`（`Draw3.RendererPrimitives.cpp:161-224`，先例 `DrawDebugMarkerShapes`）画到 backbuffer；新增一个无 SRC1 的 under blend state（参照 `RendererSnapshot.cpp:98-108`，该 PS 输出 `OperatorOutput` 但 under blend 只用 target0，shape 20 已如此用） | **不改 HLSL/.cso/Win7 guarded 字节码**；PS 已输出预乘（`inkPixelShader.hlsl:554-557`）；仅在点/线像素上着色（WARP 友好） | 需 CPU 按 dirty∩ 生成并上传：4K 点阵 100%/150%/200% DPI ≈ 14.6k/6.5k/3.7k 个 primitive（每个 32B，≤0.5MB/帧，单批容量 100k，`:167`）；网格 ≈ 250/167/125 条线；`DrawShapePrimitives` 固定 `RSSetState(rasterState)`（`:216`），需增加可选 scissor 状态（已有 `laserScissorRasterState`，`Draw3.Renderer.cpp:686-696`）；上传占用与墨迹共享的 InkData 环形缓冲 |
| R2 | 新 HLSL shape（解析点/线，目标矩形 quad，1 次 draw） | 无 CPU 生成；天然限定在目标矩形 | **shape 21/22 已被光标占用**（`inkPixelShader.hlsl:352`，VS `:77`），而 `cpu-gpu-contracts.md:111-135` 只列到 20（文档与 PS 不同步）→ 新码须 ≥23；必须按 `draw3-cursors.md:74` 同步 Win7 guarded 门限（`type > 22`）并用生成器重生成 `Draw3.Win7GuardedShader.h`（`archive/2026-10/10-18-draw3-cursor-policy/research/Generate-Win7-GuardedShader.ps1`；Win7+WARP 才用 guarded：`Draw3.ShaderCompatibility.h:9-18`, `Draw3.Renderer.cpp:755-760`），并需 Win7 WARP 复测（本环境无法验证）；WARP 下全屏 4K 约 8.3M 次 PS/帧 |
| R3（不推荐） | 小周期纹理 + wrap 采样（复用 shape 20） | 代码少 | 分数像素视口下发糊/等同滚动位图，与需求相反 |

- 4K 单帧成本（**未实测**）：平移帧已有 33MB L2→BB 拷贝、全屏 `ApplyOperatorLayers`（4 张纹理采样 + 双源混合）、快速时 9-tap 快照；图案 pass 的像素量 R1 远小于全屏（WARP：点阵 <1M 像素量级，网格 2–3.5M），GPU 上两者都小于 `ApplyOperatorLayers`。
- 实测钩子：`RuntimeMetricsCostStage::Composite`（`:18340-18341`）已覆盖合成；测量需 ARM64 Debug|Release、硬件/WARP 两路、DComp/ULW 两路。

## 3. 换页 / 换主题握手

### 3.1 现状链路

- 主题：PageControl（`PageControl.cpp:1025-1027`）→ 业务回调 → `PublishProductWhiteboardTheme`（`Draw3.Product.cpp:27`）→ `Host::PublishWhiteboardTheme`（`Draw3.Host.cpp:3420-3429`）→ `StateBridge::PublishWhiteboardTheme`（`Draw3.Bridge.cpp:164-183`）→ 绘制线程 `SetWhiteboardTheme`（`DrawingController.cpp:16016-16023` → `ApplyWhiteboardThemeCommand` `:152-165` 改 `whiteboardThemes[currentPageIndex_]`）→ `publishWhiteboard` → `ObserveWhiteboard`（style token serial++、`whiteboardStylePending` 关新 Down，`Host.cpp:1237-1243`）→ state 线程 → `Whiteboard::PublishPageSnapshot`（`Whiteboard.cpp:689` 比较 rgb‖pattern，`:693-701` 失效+取消候选+请求重绘）→ UI3 ≥2 批 + owner 回执。
- 换页：`currentPageIndex_ = targetPageIndex`（`:16442`）后同帧 `restorePageContent` + `publishCurrentPageContent` → `publishWhiteboard`；冷页先 `whiteboardLoading`（`:16426-16439`），目标页主题在加载完成时才写入 `whiteboardThemes[page]`（`:10994`）并由 `SetPage`（`:10996`, `:16093-16097`）切页 → 图案/墨迹随同一次切页。
- 今天 Draw3 的墨迹（同帧 Present）与 Freeze 底色+图案（≥50ms 后）本来就是两条异步路径。

### 3.2 方案与原子性

| 方案 | 图案 | 底色 | 墨迹 | 备注 |
|---|---|---|---|---|
| A. 仅图案迁 Draw3 | 与墨迹**同帧原子**（同线程读 `whiteboardThemes[currentPageIndex_]` 与 viewport） | Freeze 异步（现状） | 同帧 | 主题/翻页异主题时存在 ≥1 个 Freeze 周期的“图案新、底色旧”窗口；图案只有 18% alpha、颜色由亮度二值决定，错配表现为短暂低对比，不比今天“墨迹新、底色旧”更差 |
| B. 底色+图案都在 Draw3 | 同帧 | 同帧 | 同帧 | **唯一完全原子**。在同一 under 序列里再加一次 opaque 底色 under（先图案、后底色）。Freeze 不能删（白板唯一任务栏/激活锚点：`rendering-and-ui.md:1759`、`whiteboard.md:30`；`BackgroundPrepared/BackgroundMatchesActive` 仍门控 workspace Complete：`IdtState.cpp:4030,4057`），但其内容变成常量底色，可去掉主题失效/重绘（`Whiteboard.cpp:693-701`）；代价：白板下 Draw3 画布不再透明（拖动态仍走 Primary，ULW 回退+1 alpha 无影响）；Draw3 故障恢复期显示 Freeze 最后底色 |
| C. Draw3 等 Freeze ACK 再切图案 | — | — | — | 需跨线程反向握手，复杂且拖慢翻页，不推荐 |

- 样式 token 不需要改：`WhiteboardStyleToken` 含 RGB 不含 pattern（`Draw3.Bridge.h:230-238`）是有意的——笔色按 RGB 浅深分类（`IdtState.cpp:1352-1371`；`whiteboard.md:150-152`），图案只影响显示；图案迁入 Draw3 不影响该门。
- 首帧：进入白板已等待 Draw3 真实首帧/内容版本（`whiteboard.md:31`；`Draw3.PresentationState.h:17-32` 的 `DrawpadPresentationReady`），图案随首帧出现；退出/换工作区后 `activeWorkspace != Whiteboard` 即停画图案。

## 4. 若图案迁入 Draw3：Freeze/UI 路径的改动与依赖

| 位置 | 改动 | 原因/依赖 |
|---|---|---|
| `Whiteboard.cpp:130` | `drawContent` 不再调用 `DrawTheme`（仅填充底色；或改用 `background.fill = COLORREF`，注意 `:131` 现在固定为 `RGB(18,59,50)` 且被 drawContent 覆盖；RGB 转 COLORREF 参照 `Bar.Scene.cpp:565`） | `DrawTheme`（`Whiteboard.Theme.h:38-45`）唯一调用者 |
| `Whiteboard.cpp:689` | `themeChanged` 只比较 `themeRgb`（A/B 方案） | 否则仅改纹理也会整屏重绘 + 取消候选（`:693-701`） |
| `Whiteboard.cpp:62,99-100,129` | `themePattern` 保留 | 仍供 PageControl 快照（预览选中态 `Bar.Scene.cpp:755`） |
| `Whiteboard.Theme.h:38-45` | `DrawTheme` 变为无调用，可删 | `DrawThemePattern`（`:16-37`）保留 |
| 保留不动 | `Bar.Scene.cpp:810`（预览 `DrawThemePattern`，3×3 中心对称）、`PageControl.cpp:1024-1027`、`Whiteboard.BackgroundState.h` 协议、`IdtState.cpp:3875-3880 / 4026-4028`（仍传 rgb,pattern） | 预览与画布纹理不再共用同一份代码 → 需改写 spec“预览和背景共用绘制规则”（`whiteboard.md:9,53,71`；`10-15-whiteboard-v1/prd.md:10`），并增加一致性像素测试 |
| 不需要改 | `Draw3.Bridge.h`、`Draw3.Host.*`、`IdtState.cpp`、`PageControl.*` | 图案数据已在控制器 `whiteboardThemes` |

相关测试：

- `InkeysHeadlessTests/whiteboard_ui_tests.cpp`：只覆盖 `BackgroundState`（`:26-119`）、背景色分类 `IsLightWhiteboardBackground`（`:121-146`）、`PageState`（`:148-`），**无像素/纹理断言 → 无需修改**。
- `InkeysHeadlessTests/draw3_bridge_tests.cpp:1164-1204`：只验 `SetWhiteboardTheme` 的 FIFO/代次/页 → 若不新增 bridge 字段无需修改。
- `InkeysHeadlessTests/window_tests.cpp:1185-1231`（`CompleteBackgroundInactive`）不受影响。
- `Whiteboard.cpp:465-580 RunHiddenBackgroundTests`（经 `PageControl.cpp:6204` 触发）：主题失效用例 `:548-552` 通过 rgb 改变仍成立；`themePattern=2` 不再触发失效，用例仍可通过。
- 需新增：Draw3 离屏 WARP 验证函数（仿 `RunCanvasDebugOverlayValidation`，`RendererDebugOverlay.cpp:270`，注册模式见 `IdtMain.cpp:1835`）：图案关闭时与现状字节一致；墨迹/橡皮/L0 之下；分块 dirty 与整帧逐字节一致（验证 under 非幂等已被 scissor 约束）；相位 = viewport mod step；预乘合法；与 D2D `DrawThemePattern` 离屏参照对比 ≤1–2 LSB；`Draw3.HiddenWindowTest.cpp:1597-1777` 的白板段可加带 pattern 的读回。
- 需同步的规范：`runtime-and-rendering.md`（三层契约新增“白板背景 under pass，仅 backbuffer”）、`whiteboard.md:9,53,71,81`、若走 R2 还有 `cpu-gpu-contracts.md`（并补上 21/22）与 `draw3-cursors.md:74` 的 guarded 流程。

## 5. 推荐、最小变更集与风险

### 推荐

图案迁入 Draw3 合成路径：在 `DrawDebugPlateUnder` 之后追加“白板图案 under pass”，先用 R1（现有 shape 16 + 新 under blend + scissor，不改 HLSL）；底色先留在 Freeze（方案 A）。若验收要求主题切换/换页底色与图案严格同帧，再在同一序列加一次底色 under（方案 B，增量很小）。不做 Freeze 逐帧更新，不加 viewport 桥接通道。

### 最小变更集

1. `Draw3.Renderer.cppm` / `.cpp`（或新 `Draw3.RendererPattern.cpp`）：`DrawWhiteboardPatternUnder(dirty, phaseX, phaseY, stepPx, kind, premulRgba)`；新增无 SRC1 under blend state；`DrawShapePrimitives` 增加可选 scissor 光栅状态/矩形；按 dirty∩ 在 CPU 生成点/线 primitive。
2. `Draw3.DrawingController.cpp`：成员保存 underlay 状态（工作区=白板、当前页主题、dpiScale、当前页 viewport）；在 `:18355` 与 `:10388` 两处 `DrawDebugPlateUnder` 之后调用（建议合并 helper）；`:16016-16022` 主题命令后置 `forceFullPresent`；换页/进入白板/resize 已有全帧路径，补验证。
3. `Whiteboard.cpp:130, 689`：Freeze 只画底色；失效只看 rgb。
4. 测试与 spec：见 §4。

### 风险

- **under 非幂等**：任何越出本帧重拷矩形的写入都会叠 alpha（0.18→0.33）→ scissor/dirty 必须硬约束。
- **工作区门控**：漏判会让桌面/PPT 画布不再真透明，破坏 SelectionUlw 全帧零 alpha 校验。
- **A 方案的短暂错配**：图案与底色在主题/异主题翻页时可能错配 ≥1 个 Freeze 周期（≥2 个 UI3 批次）。
- **Win7 guarded shader（仅 R2）**：新增 shape 必须重生成 guarded 字节码并在 Win7 WARP 复测；shape 码 ≥23（21/22 已占用，spec 表缺项）。
- **视觉一致性**：shader 点/线 AA 与 D2D 不同，需像素对比；预览（PageControl，D2D）与画布不再共享代码。
- **viewport 精度**：±1048576 px，需 double `fmod`；单位是像素不是 DIP。
- **4K/WARP 帧耗时未知**：需用 `Composite` 指标在 DComp/ULW、硬件/WARP 实测；R1 的 CPU 生成与上传随 DPI 变化（100% 时 ~14.6k 点/帧）。
- **拖动态 timer period 被释放**（`Draw3.TimerPeriod.h:30-43`）：与方案无关，但影响平移帧节拍，应在平移任务内一并测量。
- **未来导出**：backbuffer 不含底色/图案，需从 CPU 内容 + 主题另绘。

## Caveats / Not Found

- 所有速率/耗时均为推导或类比，仓库内没有 4K ARM64 / WARP 的合成帧耗时基线（`.trellis/spec` 与归档任务中无）。
- 未确认用户机器的实际 presenter（DComp 或 ULW 回退）与 `target_fps` 档位（默认回落 120）。
- Drawpad 与 Freeze 同 bounds 的结论来自 `Window.cpp:3206-3217` 的 prepare policy 代码阅读，未做真机核对。
- 未找到任何基于 backbuffer 的导出/缩略图/截图路径（Draw3 目录内 grep 无匹配）；如其它模块存在需另查。
- `Draw3.Win7GuardedShader.h` 为生成物，无法在本环境验证 R2 对 Win7 WARP 的影响。

## Related Specs

- `.trellis/spec/native/runtime-and-rendering.md` — 三层契约 `:703-717`、自由画布导航与可信 L2 快照 `:848-887`、热前像 `:1023`
- `.trellis/spec/shaders/cpu-gpu-contracts.md` — shape 表 `:111-135`、可信快照 `:146-195`、仿射 operator `:197-218`
- `.trellis/spec/native-desktop/whiteboard.md` — 主题/纹理 `:9,11,53,71`、Freeze 背景 `:81,87-89`、原子样式 token `:134-178`
- `.trellis/spec/native-desktop/rendering-and-ui.md` — 候选/回执协议 `:2624-2647`、Freeze 锚点 `:1759`
- `.trellis/spec/native-desktop/draw3-cursors.md:74` — 新增 GPU shape 与 Win7 guarded 字节码同步
- `.trellis/spec/shaders/build-and-encoding.md` — FXC/`.cso` 构建链
- 归档：`.trellis/tasks/archive/2026-08/08-11-bar-window-dirty-present/prd.md`（整屏 `GetDC(COPY)`/ULW 成本背景）

## Files Found

| File Path | Description |
|---|---|
| `Inkeys/Inkeys/UI/Whiteboard/Whiteboard.cpp` | Freeze 背景渲染/候选/回执状态机（`:217-461`），主题发布（`:680-702`） |
| `Inkeys/Inkeys/UI/Whiteboard/Whiteboard.Theme.h` | `DrawThemePattern`/`DrawTheme`（24 DIP、点半径 1、线宽 1、18% 黑/白，中心相位） |
| `Inkeys/Inkeys/UI/Whiteboard/Whiteboard.BackgroundState.h` | 背景请求/帧/失效状态 |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp` | UI3 共享调度（60/30 FPS，Retry 即下一批） |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | 合成/呈现/脏区/viewport/页与主题命令 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer*.cpp/.cppm` | under pass 先例、`DrawShapePrimitives`、可信快照、`ApplyOperatorLayers` |
| `Inkeys/Inkeys/Drawing/Draw3/Assets/inkPixelShader.hlsl`, `inkVertexShader.hlsl` | shape 型号占用（21/22 为光标） |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp` | DComp/ULW 呈现、预乘校验 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.h`, `Draw3.Host.{h,cpp}` | 样式 token、主题发布、Runtime 快照（无 viewport） |
| `Inkeys/IdtState.cpp` | state 线程：`PublishPageSnapshot`、workspace 握手 |
| `Inkeys/Inkeys/Window/Window.cpp` | 白板三窗同 bounds（`:3206-3217`） |
| `InkeysHeadlessTests/whiteboard_ui_tests.cpp`, `draw3_bridge_tests.cpp`, `window_tests.cpp` | 相关现有测试 |

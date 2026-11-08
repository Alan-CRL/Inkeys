# Research: Freeze 自绘点阵/网格随画布平移（Freeze-owned pattern follow）

- **Query**: 点阵/网格必须由 Freeze 窗口自己的 D2D/WARP 管线绘制（共享 WARP、ULW、候选/回执），随 Draw3 视口平移重算点/线位置（不滚位图）；找最优可行方案，量化速率与滞后；S5 的 Draw3 pass 保留为 constexpr 开关回退（默认 Freeze）。
- **Scope**: internal（只读；未构建、未运行、未改任何 research/ 以外文件）
- **Date**: 2026-11-06
- **数字标注**: [实测]=仓库归档里的真机数据；[推导]=由代码/实测外推；[假设]=树内无数据的工程假设，实施者须先测。行号随其它 agent 并行编辑已漂移（控制器约 +180），下文以符号为准、行号仅作定位。

## 0. 结论

1. **可行，且在 Freeze 内最优的做法**：Draw3 绘制线程经一条新的"运行时回调"直接写 Whiteboard 模块里的无锁最新视口（seqlock）并 `RenderPipeline::Request(WhiteboardFreeze)`；Freeze 的 `RenderBackground` 在 UI3 线程直接读该值，把"相位变化"当成**纯像素脏区**（`backgroundScene.Invalidate()`），不碰 `backgroundState.Invalidate()` / `CancelBackgroundCandidate()`；并把"回执确认后立刻在同一回调里画下一帧"（流水线化），使每个 UI3 批次产出一个像素帧。
2. **期望速率**（60FPS 调度上限=60 次/s；`SetFrameRate` 只接受 30/60：`RenderPipeline.cpp:619-622`）：参考机（Adreno X1-85，2880×1920，zoom2，见 §3）约 **45–60 次/s** [推导+假设]；4K@100% DPI 仅约 **14–40 次/s** [假设，最坏情形]。不做流水线（现状协议 2 批/帧）上限 **≤30 次/s**，30FPS 档 ≤15 [推导：`RenderPipeline.cpp:347-350,744-751`]。
3. **期望滞后**（相对 Draw3 墨迹）：参考机均值约 15–25 ms（≈2–3 个 120Hz Draw3 帧），最坏 40–50 ms [推导+假设]；3000 DIP/s 时 ≈45–75 DIP（2–3 个格距），12000 DIP/s 时 ≈180–300 DIP。**不满足"微小不同步"**，除非加两项：①速度前瞻（lead compensation，把相位外推到显示时刻，§4.4）；②高速淡出（Freeze 内的最近回退，§6）。1000 DIP/s 以内无需额外手段（≈15–25 DIP，<1 格）。
4. **回退顺序与用户意图一致**：Freeze（默认，WARP/CPU，不加 GPU 负载）→ 效果差则切 constexpr 开关到 Draw3（S5 代码保留，Drawpad 窗口渲染）。
5. **Top3 风险**：①4K/100% DPI 的单次更新成本（D2D 1.46 万个 FillEllipse + 3 遍全屏基底 + 7 ms 级 GetDC）可能把 UPS 压到 15–40 且无实测；②UI3 共享单线程（每批 10–20 ms 的 Freeze 回调会拖 Bar/PageControl）+ 每次 22–33 MB 的整屏 COPY 和 DWM 重传的功耗；③高速下格点混叠（每次更新跳 2–8 个格距）与 DWM/光学滞后无法在无硬件下验证。

## 1. 现有锚点复核（当前树）

| 项 | 锚点（当前） | 状态 |
|---|---|---|
| Freeze 一次更新 | `Whiteboard.cpp:380-461`：`BeginFrameDraw` → `backgroundScene.Render` → `GetDC(COPY)` → `UpdateLayeredWindowIndirect`（经 `SubmitFreezeSurface`，`IdtFreezeFrame.cpp:84-91`）→ `PublishBackgroundCandidate` → `Retry` | 未变 |
| 在途不画新像素 | `:342-379` 分支；回执读取 `TryReadCandidateReceipt`，`Queued/Executing`/取不到租约→`Retry`；成功→`backgroundState.Prepare`、`CommitPresented`、`ConsumeDamage` | 未变 |
| 候选内容 | `PublishBackgroundCandidate :189-216`：`applyBounds=false`、`verifyBounds=active`、`requirePresentationMatch=active`、`visibility=Preserve`（revision≠0）→ owner 侧 `ApplyCandidate`（`Window.cpp:3927-3975`）只核 presentationRevision/身份/bounds，平移期无任何原生写 | 已核 |
| 调度 | `DispatchState::Request` 只是原子 `fetch_or`（`RenderPipeline.cpp:320-323`）；`Scheduler::Request` 再 `SetEvent`（`:969-974`）；`Retry/Continue` 都进下一批（`:347-350`）；批间隔 1/frameRate、默认 60（`:608,744-751`）；所有客户端回调同一线程串行（`:825-879`） | 已核 |
| 视口无通道 | `HostRuntimeSnapshot`（`Draw3.Host.h:321-`）与 `WhiteboardStyleToken`（`Draw3.Bridge.h:237-244`，仅 rgb）无视口；`Host::Impl::ObserveWhiteboard :1230-1258`→`PublishRuntimeRevision :809`（条件变量）→ state 线程 `WaitForProductRuntimeRevision(…,250)`（`IdtState.cpp:3799`）→`AcceptWhiteboardStyle`+`Whiteboard::PublishPageSnapshot`（`:3875-3880`；`Whiteboard.cpp:681-704`）→`Request` | 未变 |
| S5 diff（待改为开关） | `Whiteboard.cpp:128-131`（`DrawThemeSolid` lambda，仅 rgb）、`:690-692`（`themeChanged = themeRgb != rgb`）；`Whiteboard.Theme.h:15-16` 注释、`:39-45` `DrawThemeSolid`，`DrawTheme` 已删（原体：`FillRectangle` + `DrawThemePattern(context,bounds,rgb,pattern,1,brush)`，见 `git show HEAD:Inkeys/Inkeys/UI/Whiteboard/Whiteboard.Theme.h`） | 已核 |
| 另一路径 | `drawContent` 在 `ConfigureBackgroundScene` 时按值捕获 rgb（pre-S5 还有 pattern），所以主题变化靠 `configuredBackgroundScale=0` 强制重配（`:698`、`:311`） | 已核 |

## 2. Q1 视口通道（最小延迟）

**线程/唤醒对比**

| | 现有"主题/页"通道 | 建议的视口通道 |
|---|---|---|
| 跳数 | Draw3 绘制线程 → state 线程 → UI3 线程（2 次唤醒，Host 两把锁 + 条件变量 + `ProductRuntimeSnapshot()` 整结构复制 + `pageStateMutex`） | Draw3 绘制线程 → UI3 线程（1 次 `SetEvent`），Freeze 回调**直接读原子** |
| 谁写 | `Host::Impl::ObserveWhiteboard`（仅页/主题/加载变化） | 控制器新 lambda `publishWhiteboardViewport`，经新增运行时回调到 `Whiteboard::PublishCanvasViewport` |

- **先例**：`HostRuntimeCallbacks`（`Draw3.Host.h:209-218`）的回调本来就在绘制线程直接调 UI：`drawingActivityChanged`→`Bar::NotifyCanvasDrawingStarted`（原子计数+`PostMessage`，`Bar.Interaction.cpp:10308-10317`）、`acceptedStrokeBegan`（`Host.cpp:1573-1579`）；装配点 `IdtMain.cpp:4071-4096`（位置式聚合初始化，新字段必须**追加在末尾**）。
- **不要走 Host 快照**：在 `PublishRuntimeRevision`/`runtimeRevision` 上每帧发布会让 state 线程每帧醒来复制整份 `HostRuntimeSnapshot`；视口也不属于"原子样式 token"（token 刻意只含 rgb，`Draw3.Bridge.h:237-244`）。
- **数据形状**（Whiteboard 模块内 POD，不引用 Draw3 类型，保持 UI→Draw3 只经 `Draw3.Product.h` 头文件）：`active`、`moving`（`canvasPan.Active()`）、`generation`（controller 的 `whiteboardGeneration`）、`page`、`x,y`（float，即 `InkCanvas::Viewport()`，后备缓冲像素）、`vx,vy`（px/s，内容速度，前瞻/淡出用）、`qpc`、`canvasW/H`（仅断言）、`serial`（单调，每次发布 +1，亦即"Draw3 视口帧序号"）。
- **读写**：单写者（绘制线程）/单读者（UI3），标准 seqlock（relaxed 原子字 + release/acquire 栅栏），读者自旋重试奇数/不一致；只含 x,y 时亦可压成一个 64 位原子字。写端 `if(!requestPending.exchange(true)) Request(WhiteboardFreeze)`，读端回调开头 `requestPending=false`，避免 120Hz 写端对 SetEvent 的逐帧系统调用。**Blank 主题**用原子镜像 `patternKindMirror`（`PublishPageSnapshot` 更新）早退——平移期对 Freeze 零唤醒。
- **发布时机**：两处同一 lambda，按元组 `(active,generation,page,x,y,moving)` 去重：①`updateCanvasNavigation(navigationQpc.QuadPart)` 调用点之后（`DrawingController.cpp` ≈:17248，最早）；②与 `syncWhiteboardPattern` 同位（≈:17604、:18716，兜住翻页/冷页重载 `whiteboardViewports.Restore`、`TranslateViewport`、工作区变化）。比在 viewport apply 块（≈:14312-14335）发布更全，因为页/冷页写入口不止那一处。
- **每页/代次**：通道只带"当前页"视口，不存每页副本；每页视角仍在 `InkCanvas::Viewport()` 与 `WhiteboardViewportMemory`（`:2898-2919`，`Restore`:≈:11918）。翻页同一控制器迭代内发布新页视口（冷页加载期间 `currentPageIndex_` 未切，发布的仍是正在显示的旧页，与 Draw3 显示一致）。**最新值优先、不做页匹配**：页匹配只会让相位晚于墨迹翻页 ≥50 ms（等 state 线程）；不匹配的代价是主题/底色晚于相位（异主题翻页时，图案色仍随 Freeze 自己的 `themeRgb` 一致，仅相位提前，且对墨迹更对齐）。`active=false`（离开白板、`Run()` 退出）也发布，Freeze 读到后复位 `lastDrawn`。
- **换主题/换页后不过期**：主题变化走原路径（`PublishPageSnapshot` → `Invalidate`+取消候选+`Request`）；重画时在回调里读**最新**通道值，不缓存旧相位。

## 3. Q3 单次更新成本、速率与滞后

**实测锚点 [实测]**（`.trellis/tasks/archive/2026-10/09-27-inkeys3-first-release-hardening/automation-20261003.md:52-54`）：设备 Adreno X1-85，2880×1920/60Hz，WARP+ULW，zoom2；Bar 目标 2796×1892（5.29 MP）上 **GetDC(COPY) 均值 4.5697 ms/次**，回调均值 5.8839 ms，活动成功提交间隔 max 46.2 ms。GetDC 含 D2D flush/同步，未拆分 CPU/GPU。树内无 Freeze 整屏 D2D/ULW 的实测。

**单次成本模型** T_u = 基底遍数 + D2D 点阵 + GetDC + ULW：

| 场景 | 点数/线数 [推导] | 基底 3 遍 [假设] | 点阵 N×1–4 µs [假设] | GetDC [推导：按像素线性] | ULW [推导：≤回调−GetDC] | T_u | 速率 min(60,1000/T_u) |
|---|---|---|---|---|---|---|---|
| 2880×1920 @200%（step 48px） | 2400 / 100 | 1–4 ms | 2.4–9.6 ms | 4.8 ms | ≤1.3 ms | 9–20 ms | 50–60/s |
| 3840×2160 @200% | 3.7k / 125 | 2–6 | 3.7–15 | 7.2 | ≤2 | 15–30 ms | 33–60/s |
| 3840×2160 @150% | 6.5k / 167 | 2–6 | 6.5–26 | 7.2 | ≤2 | 18–41 ms | 24–55/s |
| 3840×2160 @100% | 1.46万 / 250 | 2–6 | 14–58 | 7.2 | ≤2 | 25–73 ms | 14–40/s |

点数与 S5 测试断言同源（`draw3_canvas_pan_tests.cpp` 点阵 14000–15500 / 6200–7200 / 3500–4200，网格 248–262）。"基底 3 遍"= `BeginFrameDraw` 的 `Clear` + `DrawShape(root)` 全屏填充 + `drawContent` 的 `FillRectangle`（`Bar.Rendering.cpp:470-479`、`Bar.Scene.cpp:1138-1143`）。**速率上限还受 UI3 批次约束**：回调 >16.7 ms 时无休眠，批周期=回调时长。

**滞后（相对视口）** L = 样本年龄(0–16.7 ms，均值 8.3) + T_u（到 ULW 完成）+ DWM 取用(60Hz 面板 0–16.7，均值 8.3)；参考机均值约 31 ms（范围 17–45）[推导+假设]；扣除 Draw3 自身墨迹显示延迟（8–16 ms [假设]）后相对错位约 15–25 ms。像素：3000 DIP/s（zoom2 ⇒ 6000 px/s）→ 相对 90–150 px（45–75 DIP）；12000 DIP/s → 360–600 px（180–300 DIP）。**阶梯效应**：60 次/s 时每次更新跳 v/60：3000 DIP/s=50 DIP（2.1 格）、12000 DIP/s=200 DIP（8.3 格，视觉上是混叠/频闪而非平滑滑动）。

**更便宜的"仍是重绘位置"选项**（均不是位图滚动/平铺；保真度一律以 §5 的 D2D 参考像素测试把关）

| 选项 | 省的是什么 | 风险/限制 |
|---|---|---|
| O1 单基底遍：`background.fillOpacity=0`（root 形状不画；drawContent 已不透明覆盖全屏，输出恒等）；或给 `BarSurfaceBackgroundSpec` 加"跳过 root"开关 | 1–2 遍全屏填充（22–33 MB/遍，WARP 内存带宽型）[假设 1–4 ms] | 需像素对比确认输出不变；`BeginFrameDraw` 的 `Clear` 是共享函数不宜改 |
| O2 网格线改 `FillRectangle` 窄矩形 | 线条描边曲面细分（≤250 条，微小） | 与 `DrawLine(width=scale)` 覆盖率逐像素等价，需验证 |
| O3 `ID2D1GeometryRealization`（D2D 1.2 / Win8.1+）：一次性把"比屏幕大一格"的整块点阵实现成网格，每帧 `SetTransform(Translation(phase))` + `DrawGeometryRealization` | 把 N×(1–4 µs)的逐图元 CPU 开销变成 ~1 次提交 [假设：点阵项降至 1–3 ms，4K/100% 可到约 14–25 ms/次] | 实现是矢量网格每帧重新栅格，不是位图；平移 AA 与 `FillEllipse` 可能有 ≤数 LSB 差；Win7 无 D2D 1.2，须 QueryInterface 失败回退 O0；设备丢失/DPI 变化需重建 |
| O4 运动期 `D2D1_ANTIALIAS_MODE_ALIASED`、静止/末帧恢复 PER_PRIMITIVE | WARP 覆盖率计算 [假设，收益未知] | 点边缘锯齿，需产品确认；末帧必须 AA 以与 (0,0) 现状逐像素一致 |
| O5 `ID2D1CommandList` 回放 | 仅省 CPU 侧枚举 | 仍逐图元栅格，收益小，不推荐 |
| O6 CPU 直写 DIB（绕开 D2D+GetDC，仅 ULW） | 理论上省掉 4.8–7.2 ms 的 GetDC 与 D2D | **离开 D2D/WARP，违反用户对管线的限定**，仅作终极后备并须用户批准 |

**测量方案（实施者先做，不要先写功能）**：`RunHiddenBackgroundTests`（`Whiteboard.cpp:466-581`，入口 `Inkeys.exe --page-control-hidden-test` → `PageControl.cpp:6204`）已用真实 WARP + 真实 Freeze HWND（屏外 `{-30000,-30000,-28080,-28920}`=1920×1080，`hiddenBackgroundTarget` scale 固定 1.0）+ 真实 owner，并以 `hiddenBackgroundPixelCount`（`:454`）计 ULW 次数、`Step()` 绕过调度器直驱 `RenderBackground`。新增 Debug/harness 用例：①把 `hiddenBackgroundTarget` 的 scale 参数化并跑 1920×1080@1.0、2880×1920@2.0、3840×2160@1.0/1.5/2.0；②themePattern=1/2，以合成 120Hz 视口样本驱动 3 s；③记录每次 `Step` 的 QPC 总时、`FrameStage` 分段（Draw/GetDC/ULW，复用 `FrameStageTimer`，`PageControl.cpp:1584-1687` 已有用法，Freeze 目前未用）、每秒 `hiddenBackgroundPixelCount` 增量、回执等待批数；④分别测 O0/O1/O3。屏外位置不含真实 DWM 上传，故这里得到的是**回调侧上限**，不是屏幕 FPS；真机仍须在目标 ARM64 机运行同一 CLI 并另做肉眼/高速摄像评估。

## 4. Q2 Freeze 内"只重画相位"的路径与不变量

### 4.1 为什么不能 `Invalidate()` + `CancelBackgroundCandidate()`
- `backgroundState.Invalidate()` 递增 `contentRevision_` 并清 `prepared_`（`Whiteboard.BackgroundState.h:106`），配合 `CancelBackgroundCandidate`（`Whiteboard.cpp:170-174`）会每帧取消在途回执；连续平移时永远追不上，且 `Prepared()` 被清会让 `BackgroundPrepared/BackgroundMatchesActive` 反复为假。规范只把"主题、DPI、设备、请求"列为退休像素候选的原因（`rendering-and-ui.md` ≈L2641），**视口相位不在其内**。
- 因此相位变化 = 场景脏区（`backgroundScene.Invalidate()` 即 `FullDamage`），不是内容失效。注意 `backgroundScene` 的 damage 只在**回执确认时** `ConsumeDamage`（`:371`）：**绝不能在在途期间调用 `backgroundScene.Invalidate()`**（否则确认时会吞掉新脏区）；只在"无在途提交"分支（`:380-383` 前）里由 UI3 线程自己判断并 Invalidate。写端线程只写原子+`Request`。

### 4.2 状态机映射（Idle/Prepared/Queued/Executing/Retry）
1. Idle，通道 `serial` 变化（或 `!moving` 且 `serial≠drawnSerial`）→ 判相位位移 ≥ε 或终帧 → `backgroundScene.Invalidate()` → 画 → ULW → 发布候选 → `Retry`（Queued/Executing）。
2. 在途期间通道继续被新样本覆盖＝**天然最新者胜出的合并**，无队列；该期间只轮询回执（`:342-379`），不画。
3. 回执 `Committed` 且身份/几何/presentationRevision 全匹配 → `backgroundState.Prepare(pending.frame)`（`prepared_` 刷新为**新帧**，其 `presentationRevision` 随每次 ULW 的 `PixelsChanged` 递增，旧 prepared 在途期间暂时失配，仅使 workspace 握手多等一轮）→ `CommitPresented`、`ConsumeDamage`。
4. **流水线（关键）**：确认后若通道又前进，先 `commitLease.Reset()`（同 role 掩码，必须先释放才能再 `TryAcquirePresentationLease`）、保持 `renderLock`，直接落入"画"分支画下一帧；否则返回 `Idle`（`animationActive` 则 `Continue`）。非流水线则只能返回 `Continue`，下一批才画，速率腰斩。
5. **结束帧**：无需"结束事件"。最后一次发布（`moving=false`、最终 x,y）使 `serial≠drawnSerial`；步骤 3/4 保证确认后再画一次且此时不应用 ε 门限（精确相位），之后进 `Idle`。
- ε：运动期相位位移 <0.25–0.5 px 不重画（惯性尾巴）；`!moving` 时任何差异都重画。

### 4.3 必须保持的不变量
- **workspace Prepare/Complete 握手**：`BackgroundPrepared(rev)`/`BackgroundMatchesActive(true,rev)` 依赖 `Prepared()` 的 `request/contentRevision/hwnd/generation/bounds/presentationRevision`（`Whiteboard.cpp:724-742`、`BackgroundState.h:139-148`）。原生 Complete 本身只核 hwnd/generation/bounds（`WorkspacePreparedMatches`，`Window.cpp:3109-3122`），不核 Freeze 的 presentationRevision，故平移期 ULW 不破坏原生 Complete；但 Whiteboard 侧每次确认都必须用新帧 `Prepare`，且 `:316-322` 的"prepared 与 surface 不符→Invalidate"不得被我们引发。
- **任务栏/激活锚点**：Freeze 仍是白板唯一锚点（`rendering-and-ui.md` ≈L1759、`whiteboard.md:30`）；相位帧的候选必须保持 `Preserve/applyBounds=false/verifyBounds`，不 Show/Hide/promote/SetBounds。
- **ULW 租约/所有权**：先 `TryAcquirePresentationLease(Freeze)`（取不到→`Retry`，不等待）；`SubmitFreezeSurface(hwnd,&info,true)` 要求 `WhiteboardFreezeSurfaceOwned()`；成功后 `PixelsChanged(Freeze)` 取新 revision（`Window.cpp:4802`）；`requirePresentationMatch` 保证迟到旧候选在新像素之后被 owner 拒绝（`:3935`）。
- **只在无在途提交时画**（`:55-56` 注释的合同）；最小化/inactive 分支（`:230-291`）在前，相位逻辑不得越过。
- **直接 `SubmitFreezeSurface` 不走候选的"轻量路径"**：合法性存疑（规范"完整 ULW **和当前精确 native 回执**都成功才提交 damage"，≈L2640），需要改规范；收益也有限——流水线下候选并不在显示路径上（ULW 时像素已交给 DWM），速率同为 1 帧/批，仅在 owner 回执慢于 1 批时才有差别。**仅当实测 owner 回执延迟（`receipt.latencyMicroseconds`，`Window.cpp:2219`）普遍 >1 批时再考虑**，且须"ULW 成功后立即用新帧 `Prepare`、仍持租约、不改可见性/bounds、仅限 Active 已 Complete 且无 workspace 在途"。

### 4.4 速度前瞻（可选第二阶段）
通道带 `vx,vy,qpc`；Freeze 用 `phase = viewport + v × (now−qpc + L_pipe)` 作相位（`L_pipe` ≈ T_u + DWM，初值 20–30 ms [假设]，上限 50 ms，`!moving`/速度<阈值时置 0）。惯性自由滑行为闭式、速度平滑，外推误差小；残余误差≈速度估计误差×前瞻≈3000 DIP/s 下 ±9–15 DIP [推导]。拖动期速度较噪，可用最近两样本差分并限幅。由 §7 的"滞后"指标决定是否启用。

## 5. Q4 相位数学、DPI、边界

- **公式**：`phasePx = fmod(W_f/2 − viewport.x, step) (+step 若<0)`，`step = 24 × scale`（px，double）；Freeze 在 DIP 空间绘制（场景变换 = Scale(scale)，`Bar.Scene.cpp:1141`），所以 `phaseDip = phasePx / scale`，`stepDip = 24`。可直接复用 S5 的纯头文件 `WhiteboardPatternPhase(halfExtent, viewport, step)`（`Draw3.WhiteboardPattern.h:47-56`，已含 NaN/∞/-ε 防护，且已被 `draw3_canvas_pan_tests.cpp` 覆盖）。网格线序号公式同 `EnumerateWhiteboardPatternPrimitives`（枚举时留 `halfWidth+3` 边距，把落在边缘外 <半线宽的点/线也画出来；pre-S5 `DrawThemePattern` 从"≥left 的首个格点"起画，边缘半点会缺，(0,0) 与旧像素一致所以新函数在 phase=旧值时必须与旧输出逐像素等价、仅额外补边缘半点）。
- **符号**：`ScreenToCanvas = screen + viewport`（`Draw3.CanvasNavigation.cpp` `ScreenToCanvas` ≈:55，该文件已被重写，行号以符号为准），故格点屏幕位置 = `W/2 + k·step − viewport`；viewport=(0,0) 时 = 现状。
- **单位**：viewport 为后备缓冲像素（`ApplyToCanvas` 把 DIP 位移除以 `dipPerPixel`，`DrawingController.cpp` ≈:2826-2843）；float，±1048576 时精度 0.125 px，所以相位必须 **double fmod**（S5 同）。viewport 可为小数像素：D2D 在 DIP 空间直接按小数中心 AA 绘制，无需取整；**平移跟随与 DPI 一致性无关**（相位对 step 周期取模，位移始终=viewport 位移），Draw3 的 `configuration_.dpiScale`（启动时 `GetDpiForWindow`，`Draw3.Host.cpp:62-80,2245`）与 Freeze 的 `effectiveDpiX/96` 不一致只会导致格距不同，不会漂移。
- **尺寸/显示器**：白板三窗（Freeze/DrawpadPresentation/Drawpad）都在 `PrepareGeometry` 里被 `SetWindowPos` 到同一 `display->Primary()` 的 `rcMonitor`（`Window.cpp:3169,3200-3217`），Freeze 目标同取 `Primary()`（`Whiteboard.cpp:79-90`）；多显示器时仅主屏。因此 `W_f == canvasW`（通道里带 `canvasW/H` 仅作诊断断言；相位中心用 Freeze 自己的 `W_f/2` 以保 (0,0) 与旧像素一致）。未做真机核对。

## 6. Q5 停手/惯性/翻页/主题

- **跟随直到末帧**：见 §4.2。末帧无 ε、精确相位；`moving=false` 的发布即便 x,y 未变也要发（让 Freeze 做"落定"检查）。
- **翻页**：同一迭代发布新页视口（含冷页被驱逐后 `Restore` 的会话内视角，`DrawingController.cpp` ≈:11918），Freeze 下一次更新直接跳到新相位（单次跳变，非滑动）。
- **换主题**：`PublishPageSnapshot` 在 Freeze 开关下 `themeChanged = themeRgb != rgb || themePattern != pattern`（pre-S5 行为）→ `Invalidate`+取消候选+重配，重画读最新相位。
- **离开白板/最小化/Resize/DPI**：`PublishActive(false)`、`WhiteboardWindowGroupMinimized`、`Display` 订阅（`Whiteboard.cpp:612-622`）走原路径；通道 `active=false` 使 `lastDrawn` 复位。
- **高速淡出（Freeze 内最近回退）**：通道速度 |v|>v2（建议 ≈4000 DIP/s，[假设]）时图案 alpha→0（等价只画纯色、并跳过重画），<v1（≈1500）线性恢复；v 越高格点越混叠、图案本就不可辨，这是用"不画"换"不错位"。需产品确认；仅在 §7 指标显示 >3000 DIP/s 不可接受时启用。

## 7. Q6 最小变更清单（无代码）与测试

### 7.1 单一 constexpr 开关（a）
- 放在 `Draw3.WhiteboardModel.h`（零新增工程项）：`enum class WhiteboardPatternOwner : uint8_t {Freeze, Draw3}`、`inline constexpr kWhiteboardPatternOwner = Freeze`，及 `kWhiteboardPatternOwnedByFreeze/Draw3` 两个 bool constexpr。可达性已核：`Whiteboard.cpp:24` `Draw3.Product.h` → `Draw3.Bridge.h:4` → `Draw3.WhiteboardModel.h`；控制器经 `Draw3.WhiteboardPattern.h:6`（`DrawingController.cpp:59`）；`IdtState.cpp:55` 已用同一链；`whiteboard_ui_tests.cpp:8` 直接 include。`Whiteboard.Theme.h` 不引 Draw3 头，也不需要（选择发生在 `Whiteboard.cpp` 的函数体内，非模板代码里 `if constexpr` 的弃用分支仍会编译，满足"代码保持编译"）。

### 7.2 Freeze 侧（开关=Freeze 时）（b）
- `Whiteboard.Theme.h`：补回 `DrawTheme(context,bounds,rgb,pattern)`（pre-S5 原体）并新增带相位的变体（DIP 相位，补边缘半点）；`DrawThemePattern`（预览，3×3 中心对称）与 `DrawThemeSolid` 一字不改；更新 `:15-16` 注释使其与开关无关。
- `Whiteboard.cpp`：`ConfigureBackgroundScene :128-131` 按开关选 drawContent（Freeze：捕获 rgb+pattern、相位读文件内静态；Draw3：现 S5 纯色 lambda）；`PublishPageSnapshot :690-692` 按开关取 `rgb` 或 `rgb||pattern`；新增静态：通道 seqlock、`requestPending`、`patternKindMirror`、`PatternFollowState{lastDrawnSerial,lastDrawnPhase,drawnQpc}`；`RenderBackground` 四处改动（读通道并决定 Invalidate；确认后流水线；记录；复位）；`RunHiddenBackgroundTests` 增例。
- `Whiteboard.cppm`：导出 `CanvasViewportSample` POD 与 `PublishCanvasViewport(const CanvasViewportSample&) noexcept`（`initialized` 为假时直接返回，无锁）。
- 新纯头文件 `Whiteboard.PatternFollow.h`（seqlock、"是否画/是否终帧/ε/前瞻/淡出"的纯规划函数、相位→DIP 转换），无 D2D 依赖，供生产与 Headless 测试共用；登记 `Inkeys.vcxproj`(+filters)。

### 7.3 Draw3 侧（开关=Freeze 时 S5 钩子惰性，代码仍编译）（c）
- **单点门控**：`syncWhiteboardPattern`（`DrawingController.cpp:11770-11787`）里 `whiteboard = activeWorkspace==Whiteboard && kWhiteboardPatternOwnedByDraw3`。因此 `whiteboardPatternWorkspace_=false`→控制器 `DrawWhiteboardPatternUnder`（≈:11249）首行即返回；`ResolveWhiteboardPatternForFrame(false,…)` 得到 `Enabled()==false`→`InkRenderer::DrawWhiteboardPatternUnder` 空操作（`Draw3.Renderer.cppm:345` 注释"未启用时为空操作"）；`MakeWhiteboardPatternKey` 恒为 `{}` ⇒ 不再触发 `forceFullPresent`。`CompositeLayersToBackBuffer`（≈:11245）与平移分支（≈:18810）两处调用点不必改。
- 新 Freeze 钩子在开关=Draw3 时对称惰性：`publishWhiteboardViewport` 首行 `if constexpr(!OwnedByFreeze) return;`，`IdtMain.cpp:4071` 的新增尾部 lambda 在开关=Draw3 时不装配。
- 新增改动点：`DrawingControllerRuntimeObserver`（`Draw3.DrawingController.cppm` ≈:99-113，加 `whiteboardViewportChanged`）；`HostRuntimeCallbacks`（`Draw3.Host.h:209-218` 末尾追加）；`Host::Impl::ObserveWhiteboardViewport`（仿 `ObserveWhiteboardThemeEdited :1218-1223`）+ `observer.whiteboardViewportChanged=` 装配（≈`Host.cpp:2264-2270`）；控制器 `lastCanvasPanVelocityPx` 暂存（取 `CanvasPanInput::Step` 返回的 `frame.velocityPixels`，现仅在 `updateCanvasNavigation` 内部可见）+ `publishWhiteboardViewport` lambda 与上述调用点；`Draw3.WhiteboardModel.h` 放 `WhiteboardViewportSample` POD（或 Bridge.h）。

### 7.4 S5 产物清单（保留，仅惰性）
`Draw3.WhiteboardPattern.h`（含 `Inkeys.vcxproj:419`）、`Draw3.RendererPattern.cpp`（`Inkeys.vcxproj:~1146`、filters）、`Draw3.Renderer.cppm`（include:17、`RunWhiteboardPatternValidation:200`、`DrawWhiteboardPatternUnder:345`、friend:389、成员 :459-463）、`Draw3.Renderer.cpp:527-529`、`Draw3.RendererPrimitives.cpp`（`rasterStateOverride`）、控制器（`DrawWhiteboardPatternUnder`、成员 `whiteboardPatternWorkspace_/Kind_/Rgb_`、`syncWhiteboardPattern` 及两处调用 ≈:17604/:18716、两处合成调用）、`IdtMain.cpp:1837-1844`（`--draw3-whiteboard-pattern-test`）、测试 `draw3_canvas_pan_tests.cpp` 的 `TestWhiteboardPatternGeometry`（≈:1364-1514）及注册。无需删除任何一项。

### 7.5 S5 现有测试/断言的开关敏感性（d）
| 现有项 | 是否随开关变 | 处理 |
|---|---|---|
| `TestWhiteboardPatternGeometry`（纯 CPU，直接调 `Resolve…/Phase/Enumerate/Key`） | 否 | 不改；另加开关常量与"门控纯函数"的断言 |
| `RunWhiteboardPatternValidation` 全部 GPU 检查（显式传 params 给渲染器） | 否，必须在默认 Freeze 下仍通过（要求(c)） | 不改 |
| 同函数末段 `"Freeze draws only the opaque theme colour (no pattern pixels)"`（≈:640-666，实际测的是 `DrawThemeSolid`） | 文案会误导 | 改文案为 `DrawThemeSolid` 辅助；新增 Freeze 用例：新相位函数在 phase=(0,0)、旧参数下与 `DrawThemePattern` 参考逐字节一致 |
| `RunHiddenBackgroundTests :549-553`（`themeRgb` 与 `themePattern` 同时改且手动 `Invalidate`） | 两种开关都通过 | 新增：Freeze→仅改 pattern 必须使 `Prepare(旧帧)` 失败；Draw3→不得失效（S5 的"仅 RGB"规则目前无测试） |
| `whiteboard_ui_tests.cpp`、`draw3_bridge_tests.cpp:1164-1204` | 否 | 不改 |
| PRD R6/AC5、design.md/implement.md、`whiteboard.md`/`rendering-and-ui.md`/`runtime-and-rendering.md` | 文字与 S5 现状冲突 | 由协调者改：写明开关、默认 Freeze、"相位帧不是内容失效、确认后流水线化" |

### 7.6 分阶段测试计划
1. **Headless（无 GPU）**：①seqlock 双线程撕裂压力（x==−y 不变量，≥1e6 次）；②规划函数：最新者胜出、在途不画、确认后流水线、终帧无 ε、Blank 零唤醒、`active=false` 复位、前瞻限幅/淡出阈值；③相位：scale 1/1.1/1.25/1.5/2、viewport ±1048576、小数、负数、周期性（复用 `WhiteboardPatternPhase`）；④控制器发布去重纯函数（首次/位移/换页/代次/`moving` 翻转/离开白板仅发一次 `active=false`）；⑤开关门控纯函数（Freeze 下 `DrawnByDraw3(true)==false`）。新文件 `whiteboard_pattern_follow_tests.cpp` + `animation_tests.cpp` 注册 + `InkeysHeadlessTests.vcxproj`。
2. **离屏 WARP（复用 `RunWhiteboardPatternValidation` 的 D3D11 WARP + D2D + staging 读回夹具，`Draw3.RendererPattern.cpp:258-312`）**：①phase=(0,0) 与旧 `DrawThemePattern` 逐字节一致；②整数相位：渲染结果 == 相位 0 渲染按像素平移（D2D 整数平移应逐字节，容差 ≤1 LSB）；③小数相位对 S5 Draw3 着色器（锚点平移的 D2D 参考，S5 已用 `maxDelta ≤12`）的跨 owner 一致性；④边缘半点存在；⑤O1/O3 变体与基线像素对比。
3. **hidden-WARP 计时**：§3 测量方案；`--page-control-hidden-test` 的真实 owner 回执链路下断言：平移样本流中 `Prepared()` 始终为最新帧、在途期间不再画、末帧后 `serial==drawnSerial`、`BackgroundMatchesActive(true,rev)` 在确认后为真、无 `contentRevision` 递增（`ContentRevision()` 不变）。
4. **无法在无硬件下验证**：真实 DWM/vsync 取用延迟与光学错位（需高速摄像）、目标 ARM64 机在 4K/各 DPI 的 D2D/GetDC/ULW 绝对耗时、UI3 与 Bar/PageControl 同线程争用、功耗/温度、跨显示器与平移中 DPI 变化、Win7（O3 回退）、owner 回执真实延迟分布。

## 8. 两项"仅显式诊断"指标（供真机评估）

沿用仓库既有惯用法 `INKEYS_SETTING_ENTRY_PROFILE`（`Setting.cpp:44,168-188`；`Inkeys.vcxproj:1041` 仅 Debug 定义宏 + 环境变量 `=1` 才采样；固定 512 样本数组，不逐帧分配/格式化/写盘；结束汇总一次，Release 不编译）：新宏 `INKEYS_WHITEBOARD_PATTERN_PROFILE`，环境变量同名。关闭时零开销（不读 QPC、不写样本）。

- **M1 Freeze 图案更新率（updates/second）**：在 `RenderBackground` 中 `presented==true` 后（`Whiteboard.cpp` ≈:453，与 `hiddenBackgroundPixelCount` 同位）记一次"相位更新"（排除主题/DPI 触发的整屏重画）；以通道 `moving` 的 true→false 为一个平移片段，汇总：片段时长、更新次数、**UPS=次数/时长**、相邻更新间隔 p50/p95/max、回调总时与 `FrameStage` 分段（Draw/GetDC/ULW）、确认等待批数、被丢弃的在途提交数、Draw3 发布率（Δserial/Δt，作为"帧"的单位）。
- **M2 图案落后视口的帧数（lag in frames）**：每个已画更新，`lagFrames = serial(ULW 完成时通道最新) − serial(本次所画样本)`；同时在回执确认时再取一次（软件上界，含一批等待）；并给 `lagMs = now − sample.qpc` 与 `lagPx = |viewport(now) − viewport(served)|`，汇总 p50/p95/max；另报"落定时延"=最后一次 `moving=false` 发布到对应帧 ULW 完成的毫秒数。**不含 DWM 扫描输出**，所以是软件侧下界，光学滞后需外部摄像。
- 通道 `serial/qpc` 的写入若仅为 M1/M2 则同样受宏门控；若启用前瞻（§4.4）则 `qpc` 常开（每次发布多一次 QPC≈数十 ns）。

## 9. Q7 推荐

**单一最佳方案**：Freeze-owned、事件驱动、流水线化的相位跟随（§2 通道 + §4 路径 + §7 开关），先做 O1（单基底遍），按 §3 实测决定是否上 O3；前瞻与高速淡出作为可开关的第二阶段，由 M1/M2 数据决定。
**是否能满足"微小不同步"**：参考机（2880×1920@200%）在 ≤1000 DIP/s 可以；≈3000 DIP/s 需前瞻才能把错位压到 <1 格；≥6000 DIP/s 或 4K@100% 不能（更新率 14–40/s、每次跳 ≥2 格、混叠），此时最接近的 Freeze 内做法是"高速淡出"（§6），而非降画质。若 M1/M2 在目标机上仍差，按用户既定顺序把开关切到 Draw3（S5 路径、Drawpad 窗口渲染）。

## Caveats / Not Found
- 所有 Freeze 单次耗时、D2D 单图元成本、DWM 取用与 owner 回执延迟均无树内实测（仅 §3 的 GetDC 4.57 ms 为 [实测]，来自 Bar 目标而非 Freeze）；"基底 3 遍 1–4 ms""每图元 1–4 µs""Draw3 自身显示延迟 8–16 ms"是假设，先测再定。
- `ID2D1GeometryRealization` 在 WARP + 本项目 D2D 设备上的可用性/AA 一致性未验证；Win7 无此接口。
- 未确认用户机器实际 DPI 缩放、显示器刷新率与 `config.UI.FrameRate`（默认 60）；本文把 2880×1920/zoom2/60Hz 当参考机仅因归档记录。
- 通道/流水线均为设计，未写代码；控制器行号已漂移，实施时以符号检索。

## Related Specs
- `.trellis/spec/native-desktop/rendering-and-ui.md` — 候选/回执协议 ≈L2624-2647、Freeze 锚点 ≈L1759、诊断惯用法 L2366
- `.trellis/spec/native-desktop/whiteboard.md` — 三工作区交接（Freeze 背景/Prepare/Complete）、主题合同
- `.trellis/spec/native/runtime-and-rendering.md` — 自由画布导航与视口语义
- 前序研究 `research/background-pattern-ownership.md`（§1 的"逐帧不可行"结论在本文被用户指令覆盖，且本文给出具体速率/滞后数字）

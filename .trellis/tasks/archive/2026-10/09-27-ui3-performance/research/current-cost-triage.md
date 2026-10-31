# UI3 当前成本链只读复核（2026-09-27）

范围：当前 `chore/publish` 工作区生产 UI3 Bar、共享 RenderPipeline、既有 08-09/09-22 任务。只读代码与任务记录；未启动 HWND/GUI，未运行 MSBuild、性能采样或新的测试。以下“已修”表示当前调用链可见的实现与历史自动验证，**不表示当前整帧性能或故障用户体验通过**。本研究未改产品、规范或父任务账本。

## 分段成本与所有权图

| 阶段 | 当前生产链和可观测边界 | 风险/不能混淆的成本 |
| --- | --- | --- |
| 输入、唤醒与共享调度 | Bar 输入/状态变更使用 RenderPipeline 的按客户端请求位；`RenderPipeline.cpp:297-326,480-530` 合并请求、处理 idle event、按至多 60 Hz 取批。`576-649` 固定顺序串行回调 Bar、四个 PageControl、Settings 等；Settings 可见时 `Setting.cpp:8303-8323` 的 `Present(1,0)` 也在该共享线程。 | 某次 Bar 回调本身快，不代表到下一次 Bar 回调的间隔短；Settings/其他窗口、设备恢复、日志 sink 和调度等待应由 `UI3Diag` 各客户端时间与 active gap 分开归因。不能由鼠标事件数直接推断绘制帧数。 |
| 帧快照/时间 | `Bar.RenderLoop.cpp:959-1003` 读 demand generation、alpha 修订、`FrameAnimationClock::Tick()`、Bar zoom 与锁内 `GetStateModeSnapshot()`；Work 1 正在把同一帧状态读收敛到一个快照。`13037-13240` 还消费直拖及底栏 seqlock tuple。 | `Tick()` 是回调时间，不是成功像素呈现时间；`FramePacing.cppm:100-109` 仍将正 dt 限为 50 ms。当前 backoff 判断在 Tick 之后（`RenderLoop.cpp:13041-13045`），退避跳过的 dt 没有推进动画。 |
| target/layout | `RenderLoop.cpp:13230-13242` 每个通过退避/交接门的回调都调用 `ApplyDisplayTransition`、`SubmitTargetsAndLayout`（`1214` 起，范围至 `5332`）与 `AdvanceAnimationsAndDeriveLayout`（`5333-7810`）。 | 局部变化可能仍执行大范围目标/布局计算；当前无全链路逐 domain 耗时，不能认定它是主因。H0 Release 的 57 值扫描 median 约 173/194 ns 且噪声大，只能排除把单项扫描当已证明的大瓶颈。 |
| 动画与光照 | 动画推进/eraser derive 后，`7812-7924` 消费 render-once、sustain、动态光照，发布跨 Scene 光照快照，计算视觉 demand/dirty；光照订阅广播 `Bar.Scene.cpp:1899-1924` 已按本次实际 damage 才唤醒 Scene。 | 09-23 已删除颜色块关闭时的 22 个冲突 `ft.SetTar(1.0)`，并减少无交集 PageControl 光照唤醒；仍需确认本机动态光照真正提交与缓存成本。旧光、glow 与 AA 必须纳入 damage。 |
| 资源准备与绘制 | `RenderLoop.cpp:7926-9259` 先算目标/容量并 `EnsureDeviceResources`；`9260` 起合成 dirty；`9880` 起 D2D draw。`Bar.Rendering.cpp:1469-1647` 的圆角父 A8 mask 用半径/描边/模糊四分之一像素 key、最多 24 项；`2066-2243` 几何父 mask 按尺寸/variant/描边/模糊 key、最多 24 项。`1677-1679,2019-2020` 单独计真正提交的 `FillOpacityMask` 分片。 | 父 mask hit 只省 Gaussian 创建，不省 D2D 分片：09-22 原生产函数计数探针给 15 对象在 zoom 1/1.3 分别 135/375 次提交，未测真实 GPU/驱动毫秒。父缓存有数量上限但没有全局字节上限；是否形成压力取决于实际尺寸/工作集，不能据此认定泄漏。 |
| SVG / 路径 | `Bar.Rendering.cpp:2723-2819` 先检查可见性、色/基础尺寸/放大阈值；需更新时调用 `Bar.UI.cpp:400-489` 进行 UTF16→UTF8、颜色占位符替换、lunasvg 解析、CPU raster、D2D `CreateBitmap` 上传，最终每次调用 `DrawBitmap`。内容切换还在 `Bar.UI.cpp:329-369` 解析目标宽高；`Bar.Rendering.cpp:2533-2631` 主按钮超椭圆按宽高/n/segments 建局部路径，位置变化只建变换几何；当前主绘制仅 `RenderLoop.cpp:11531` 调一次。 | SVG 已有逐实例位图缓存，尺寸动画会尽量复用并只在材质放大时刷新，不能宣称每帧重复解析；颜色动画、内容切换、尺寸门槛与设备 epoch 仍可能 miss。当前 `UI3Diag` 没有把 SVG 解析、raster、上传与 draw 单列，主按钮路径的 `powf`/几何构建也无阶段耗时。`DiscardDeviceDependentCaches`（`Bar.Rendering.cpp:167-219`）清位图/遮罩/几何，内容文本保留。 |
| 呈现与成功事务 | draw 在 `RenderLoop.cpp:12555` 停表并弹出 clip；`12566-12654` 分别测直拖呈现锁、`GetDC(COPY)`、ULW、`ReleaseDC(empty RECT)`、`EndDraw`。`12687-12724` 四阶段由 `BarPresentDecision::CompleteAttempt` 统一判定并只在 commit 后推进 dirty/viewport/业务快照；失败保持全脏重试。 | `GetDC` 可 flush 前面的 D2D/GPU 工作，不能把长 `GetDC` 全归咎于内存复制；`ULW` 成功、`EndDraw` 成功及实际事务 commit 分开。`FrameResult::Retry` 可为布局交接，不能当错误。真正用户可见/光学时间需要 GUI/设备测量。 |
| 等待/恢复 | `RenderLoop.cpp:12941-12945` 在 Bar 报 Idle 前 `Rebase()`，Scheduler 统一 sleep/pacing；失败仍返回 Retry 并按 `BarPresentDecision` 退避。 | 现源码在 idle **进入时** Rebase，未见 idle **唤醒后**再次 Rebase；长 idle 首回调理论上取得 50 ms clamp，跟 08-09 的“post-wake rebase 已修”及现行 rendering spec 不一致。需用生产 FrameAnimationClock + Scheduler 的确定性时间测试查清当前真实接线；不要只改 duration 或盲删 clamp。 |

## 历史结论在当前代码中的状态

| 项目 | 当前静态结论 | 尚欠证据 |
| --- | --- | --- |
| 08-09 大型优化 | 动画/布局拆分、dirty 成功事务、mask/brush 缓存、独立 scheduler 与基准曾完成；当时记录的 D2D combined P50 `2.564→1.659 ms`/`2.990→1.735 ms` 属于旧场景/旧提交，不能作为 H0 或 HF 对 HC/H2 的胜出数据。 | 当前生产、Release、同机真实 Bar/ULW/光影重测。旧任务声称 idle post-wake Rebase 与当前源码不符，需要针对性回归。 |
| 09-23 S1/S2/S3 | 当前源码保留颜色块目标去重、`Bar.Scene` 有效 damage 才 wake、Bar/PageControl `ReleaseDC(empty RECT)`。历史真实动画模块/headless/ARM64 Solution 通过。 | 完整 Scene hook 计数、实际损伤区域/光影视觉、用户偶发卡顿现场。S3 是语义修正，历史无 HWND D2D 探针未测到稳定耗时收益。 |
| 09-23 S4 诊断 | `FrameDiagnostics` 区分 callback、实际 animation advance、GetDC/ULW 尝试、commit、defer/backoff、mask hit/miss/create/slices 和分段耗时；默认健康静默，异常限频。当前 `IdtMain.cpp:1204-1214` 接异步 discard-new logger。 | 日志接受入队不等于最终落盘；无当前故障样本。诊断含 callback/draw/getDC/ULW/ReleaseDC/EndDraw/present lock，不含完整 target/layout、SVG parse/raster/upload、path build 独立计时。 |
| 09-22 D1/D2/D3 | D1 时钟/恢复、D2 exact 平移、D3 capacity/viewport 被单独列为未实施方向。当前 exact 在 `Bar.Rendering.cpp:1803-1808` 拒绝一切非 Identity matrix；主栏 target 受 capacityOrigin 平移。 | D1 的实际故障/时间测试、D2 真实 D2D 像素等价及时间收益、D3 完整 layout/ULW 故障证据。不能把 D2 的分片计数直接换算毫秒。 |

## 最小可检验候选：仅允许像素对齐的整数平移进入 exact A8

**假设与边界。** 当前 `ResolveRoundedRectExactMask` 只接受 Identity；主栏 `RenderLoop.cpp:9750-9769` 在一般 capacityOrigin 非零时施加平移，而其余 exact 条件（DPI 96、半径等于父 key、局部 destination 像素对齐、8 个绘制帧预热、每项 512 KiB、总 4 MiB）可能成立。仅当 matrix 是单位缩放/无剪切/无旋转、`_31/_32` 有限且为整数设备像素、局部与变换后边界均像素对齐时，扩展 eligibility；不碰非整数平移、半径量化、大小/内存预算、失败 latch、shader、质量和最终帧缓存。zoom 1.3 的 25 片可能仍被半径量化条件挡住，不预估为 1 片。

**先测再改。** 复用 `Bar.EraserAttribute.Test.cpp:73-75` 现有不创建 HWND 的生产 WARP/D2D 离屏测试框架，加入测试专用样本（不进入默认产品热路径）让真实 `BarUIRendering` 对同一圆角、光源、stroke 与矩形分别用 Identity、整数平移和分数平移绘制；连续超过 8 个实际绘制帧预热。记录 exact hit/fallback、`FillOpacityMask` 次数、mask create/evict、离屏 Draw/`EndDraw` 时间及最终像素逐点差异，覆盖 clip/alpha/旧脏区，独立重复至少三轮且串行。若现有离屏 target 无法呈现 exact 语义或像素严格等价，停止该候选，不以 fake-context 调用数通过代替。先冻 Release 同配置 H0；最小 eligibility 修改后同样采样，若收益在噪声内或新增复杂度过大则不保留性能改动。ULW、GPU 和真实帧收益仍列人工/真机；不能把离屏结论外推。

**为何当前优先于重写扫描/缓存。** 旧 probe 已显示分片可能放大，当前代码能明确定位一个可达性门，并有现成真实离屏 D2D 模块可作质量和成本对照。没有证据支持把 57 值扫描、普通 SVG cache 存在、父 mask hit 或单次 benchmark P50 当作 H0 整帧主因。另一条应并列单独解决的正确性问题是 idle/wake Rebase 与退避 dt；需确定时间合同和生产测试，不能和 exact 性能改动混成同一 diff。

## 现场诊断判别与未测边界

- 先看 `[UI3Diag]` 同批 `Bar/Settings/PageControl` callbackMs、activeGap、requested/continued/retried；Bar 慢再看 draw/getDC/ULW/EndDraw/lock。GetDC 长可能含前面 GPU 同步。`attempt`、`ulwAttempt`、`commit`、`backoffSkip` 与合法 Retry 分开。
- 动态光影样本同时报告 parent hit/miss/create/failed、exact hit/fallback reason、slices、target/capacity/viewport/source、epoch、active light；在关闭/展开/鼠标移动和长期稳态分开统计。新资源创建与大量命中后绘制是两条不同成本。
- SVG/path 需要额外离屏分段计时，至少解析、几何构建、CPU raster、D2D upload、draw；当前诊断没有这些细目，不得写“已测”。常规 SVG 色/尺寸 miss 与设备重建后冷 miss 分开。
- 本轮没有当前 UI3 真实 renderer/presenter、HC/H2 同机轨迹、GPU/ULW 成功帧分位、动态光影视觉或用户偶发故障现场数据。所有性能结论仍是静态归因与已有受限 probe 的候选，未达到 UI3 任务验收。

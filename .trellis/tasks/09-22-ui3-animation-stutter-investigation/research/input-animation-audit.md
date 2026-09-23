# Research: UI3 输入、动画目标与坐标快照审计

- Query: 主栏偶发动画慢放/跳变、动态光影关闭后缓解；审查输入通知、动画收敛、锁与大位移目标，区分源级缺陷和实机性能根因。
- Scope: mixed；主要为本地源码，seqlock 内存序另查 WG21 与 Microsoft 一手文档。
- Date: 2026-09-22
- Active task: `.trellis/tasks/09-22-ui3-animation-stutter-investigation`
- 基线: 父会话提供当前 HEAD `94e07b25`；本研究未执行 Git 操作，也未独立比较前序 `ab023a17`。
- 授权边界: 仅调查及本任务 research 产物；未修改产品、测试、工程、spec，未构建主程序或启动 GUI。

## Findings

### 1. 已确认缺陷：11 个颜色块收起时，同帧重复设置不同 ft 目标

这是本分工范围内最具体、最值得首先修正的错误。它能确定破坏关闭过程中边框与面板几何的同步，但整体卡顿的耗时贡献仍需结合光影缓存调查和实机记录。

**生产路径与触发条件**：

1. `Bar.RenderLoop.cpp:1190` 起的布局/状态计算是每次渲染回调的阶段；颜色块设置不受“本帧是否改变选中态”门禁保护。
2. 当 `state.barState.drawAttribute == false`，`Bar.RenderLoop.cpp:3063` 的 `drawAttributeLayoutScale = BarDrawAttributeCompactScale = 60 / 370`。常量分别来自 `Bar.Main.cpp:51`、`Bar.Main.cpp:52`、`Bar.Layout.cppm:37`。
3. ColorSelect1 的选中/未选中两个分支都调用 `ft.SetTar(1.0)`：`Bar.RenderLoop.cpp:3126`、`:3131`。ColorSelect2..11 相同，共 22 处，末尾是 `:3416`、`:3421`。对收起状态而言总是进入未选中分支，但任一分支的 ft 目标一样。
4. 同一帧稍后 `Bar.RenderLoop.cpp:4187` 的 ColorSelect1..11 循环把 `w/h/rw/rh` 设置为缩小几何，并在 `:4196` 再次调用 `ft.SetTar(drawAttributeLayoutScale)`。所以隐藏/收起时每帧实际目标链是 `原目标 60/370 → 1 → 60/370`。
5. `Bar.Animation.cppm:784` 的 `SetTar` 对真正不同的目标会在 `:801`、`:802` 重设 `startV = val`、`progress = 0`。这里不是排队，只会保留最后一段，但最后一段的起点每帧又改成当前值。
6. `Bar.RenderLoop.cpp:4250` **确实还会**调用 `SyncValueDuration(ft)`。该 helper `:1325` 在 `!IsSame && progress == 0` 时覆盖 duration，且在活动批次中覆盖曲线与续段参数。因上一步每帧清零 progress，这个“只处理新段”的门禁每帧都通过。
7. 批次活动时 `Bar.RenderLoop.cpp:3030` 使用 `drawAttributeTimeline.GetRemainingDuration()`；关闭的曲线为 `EaseInBack`（`:3037`）。`Bar.Animation.cppm:419` 对 Back 续段采用重新归一化剩余段的算法。因此边框每帧重复采样 Back 的初段，无法沿面板的原始完整轨迹推进。
8. 先在 `Bar.RenderLoop.cpp:5538` 推进 ft，再在 `:7779` 推进父时间轴；不存在父时间轴先走一帧的误读。该颜色块没有稍后的 `SetDirect` 纠正；`:6169` 只给扩展入口/分隔线设置派生几何，不涵盖 ColorSelect1..11。
9. `:4308` 的 visibility change / `:4384` 的 side switch 只在对应状态变化时做一次强制重建；它们不会在以后每帧消除上述重复目标。打开或保持打开时 layout scale 为 1，两个目标相同，不发生这里的冲突。

**已执行的无 GUI 公式验证**：

- `research/input-animation-ft-model.py` 对照以上公式，以 double 精度计算 60 Hz、默认 0.4 s、从完全展开到完全收起的一条轨迹。
- `research/input-animation-ft-trace.csv` 保存全部 25 帧，包含父进度、实际应有面板比例、重复目标 ft、单目标 ft、`ft / panelScale`。
- 执行命令：`python .trellis/tasks/09-22-ui3-animation-stutter-investigation/research/input-animation-ft-model.py`；退出码 0。
- 这是源码公式模型；没有运行产品、D2D、ULW 或性能采样，不能将其称为 GUI 故障复现。

| 60 Hz 帧 | 关闭时间进度 | 正常面板比例 | 重复目标下的 ft | ft / panelScale |
| --- | --- | --- | --- | --- |
| 6 | 25% | 1.030109797 | 1.011150772 | 0.981595142 |
| 12 | 50% | 1.010472973 | 1.032478518 | 1.021777470 |
| 18 | 75% | 0.776140203 | 1.088689942 | 1.402697526 |
| 21 | 87.5% | 0.526916174 | 1.168634523 | 2.217875596 |
| 23 | 95.8333% | 0.297856255 | 1.226506684 | 4.117780523 |
| 24 | 100% | 0.162162162 | 0.162162162 | 1 |

这不是仅改变不到一个像素的普通误差：后半程边框变粗、最后一帧跳到终点；如果柔光键以 `ft / panelScale` 归一化，原本应接近不变的宽度变为多组不同值。具体缓存量化和淘汰后果由 `lighting_cache_audit` 联立调查。

**不能据此宣称永不收敛**：

- 正常 0.4 s 关闭使用越来越短的剩余时长，模型最终精确结束到 `60/370`。边框到达目标后，即使 SetTar 仍来回覆盖，最终 `val == tar`，`Bar.RenderLoop.cpp:5538` 的 `IsSame` 会跳过推进；它不会因此继续产生 `active`。
- 首次隐藏状态还有 `Bar.UI.cppm:188` 的默认 `forceReplace = true`，由 `Bar.RenderLoop.cpp:5529` 消费；首帧会直接到终点，不应描述为“启动后所有隐藏颜色块无限动画”。
- 尚未找到可由正常 dt 序列稳定触发的隐藏 ft 永动状态。精确相等判断虽然值得留意，不应把理论浮点残差直接升级为本次根因。

**最小修复建议（等待用户批准）**：删除选中/未选中分支中的这 22 处 `ft.SetTar(1.0)`，让 `:4196` 成为常规 ft 几何目标的唯一来源。保留 `:4250` 的批次同步及可见性/换边的一次性关键帧设置。这一选择符合颜色块自身的所有权：选中分支只处理勾号，最后的几何循环统一拥有尺寸、圆角、描边宽度。不要改成几何循环每帧 `SetDirect`，后者会取消边框应有的平滑缩放。

**可证伪/验证方案**：批准后使用真实 `BarUiValueClass` 与实际布局设置顺序记录同一关闭轨迹，检查边框与面板比例；在真实光影 renderer 记录同帧 mask key、cache hit/miss/create count 以及遮罩生成时间。若修正后归一键稳定、但故障中的慢帧依然集中于其他阶段，应降低它对整体卡顿的解释权重。

### 2. 输入通知有明确退出边界，未发现普通鼠标移动直接造成永久活跃的路径

- `Bar.Interaction.cpp:508` 的 `WM_INPUT` 调用 `RegisterBorderCursorLight`，然后交给 `DefWindowProcW`；本模块不为每个输入创建工作线程或渲染任务。
- 自然 `WM_MOUSEMOVE` 在 `:937` 过滤 Pen/Touch 兼容鼠标副本，在 `:941` 激活第三光源；`ActivateBorderCursorTracking` 在 `:5641` 统一检查动画/边缘光/动态光开关。
- Raw Input 仅注册 Generic Desktop/Mouse `RIDEV_INPUTSINK`，owner 为 Bar HWND（`:5606`）；Inside/Grace 只在需要时注册。
- `RegisterBorderCursorLight` 在 `:5704` 拒绝 Dormant 或未注册状态；使用当前系统 `GetCursorPos`，不回放历史每个鼠标 delta。
- `:5728` 只在首次 Inside→Grace 设置绝对 `now + 5000`；`:5733` 在后续输入检查原截止时间，移动不会续期。因此即使 WM_TIMER 受到繁忙消息影响，继续到来的 Raw Input 也能检查 deadline。
- `SuspendBorderCursorTracking` 在 `:5828` 删除 timer、进入 Dormant，`:5854` 注销；注销逻辑在 `:5571` 先清 registered/inputAvailable，即使 Win32 注销失败也不允许迟到 WM_INPUT 继续唤醒。
- 登记失败只会禁用本轮追踪；timer 创建失败会立即 Suspend（`:5741`、`:5809`）。这些状态可能维持到重启，但表现应是第三光源消失，不能用来解释光影持续高开销。
- `IsBorderCursorLightNearVisibleRegion` 在 `:5956` 仅扫描固定 10 个 Bar 区域加四个分页区域，未见与运行时间一起增长的容器。
- `Bar.Main.cpp:199` 的 `UpdateRendering(false)` 仍走统一 mutex、`WakeSignal::Notify` 和 RenderPipeline Request，但跳过 `StateUpdate`。它改变 demand generation 的恢复后果由 scheduler 调查负责；不能把这种合并请求称为无限渲染队列。

**仍需测量的条件化候选**：高报告率鼠标会放大窗口线程 `GetCursorPos/WindowFromPoint`、坐标快照、互斥锁和 Request 的调用次数；其 CPU 成本可能使其他渲染工作变慢，但没有现场计数不能判定。可统计 `WM_INPUT/s`、坐标真正变化次数、发出的 Request/s 和该函数 wall time；输入静止而仍持续慢时可以排除“每包输入处理本身”作为唯一主因。

### 3. 没有发现动态光影把全局动画速度写小的路径

- `Bar.Animation.cpp:23` 默认速度 1.0。
- 生产唯一写入口 `Bar.Main.cpp:229` 的 `SetAnimationOptions` 把有效速率限制到 `[0.1, 5]`，关闭动画时使用 `1e12` 完成普通动画；调用方是 `IdtMain.cpp:1417` 的启动配置和 `Setting.cpp:3223`、`:3256` 的显式设置。
- `Bar.Main.cpp:239` 的 `SetEdgeLightingOptions` 只写边缘光两个开关并请求第三光源休眠，不写动画速率。
- 普通几何动画用当前时间进度插值；不是每次把位移除以当前差值而可能自然失速的积分器。`SetTar` 会拒绝非有限几何目标（`Bar.Animation.cppm:787`），推进时会把非有限目标/起点/时长回落到有限终值（`Bar.Animation.cpp:130`、`:53`）。

**次级语义问题**：`Bar.Animation.cpp:135`、`:193`、`:230` 将 `dt <= 0` 解释为直接结束动画；`Bar.Animation.cppm:467` 与 `:631` 的时间轴则忽略这种 dt。零时间步因此可令几何与父时间轴不同步、产生跳跃。常规真实单调时钟是否会传入这种 dt 尚无证据，不能称为当前慢帧根因。记录原始 dt / 使用 dt / forceReplace 即可区分。

### 4. 锁审计与独立 seqlock 正确性风险

已读到的光源 mutex 临界区短，未见光源锁中调用渲染或等待系统窗口：`Bar.Rendering.cpp:559` 只复制光源快照；交互侧 `GetCursorPos`、`WindowFromPoint`、`BarScreenToLayout`、`UpdateRendering` 大多在 `borderCursorLightMutex` 外。直拖使用 `directWindowDragMutex` 的 `try_lock`（`Bar.Interaction.cpp:6499`），成功后才移动窗口。未在本分工范围找到可证明导致持续慢放的锁循环。

属性动画事务采用 `atomic_flag`，等待 64 次后 `SwitchToThread`（`Bar.Animation.cpp:25`）；被抢占的持锁线程可能放大单帧时延，但锁覆盖有限算术/原子赋值，未见 GDI/COM/文件操作混入，不能凭存在自旋锁就认定根因。

**独立可移植性风险**：

- `Bar.Main.cppm:480` 的 PendingDisplaySnapshot 和 `:522` 的 BottomDockPresentedSnapshot 都是 `serial acquire → payload relaxed loads → serial acquire`。第二次 serial load 前没有 acquire fence。
- 对应写入分别位于 `Bar.Initialization.cpp:66`、`:88`，`Bar.RenderLoop.cpp:12668`、`:12815` 和 `Bar.Main.cppm:797`、`:822`。后两套成功呈现写入由同一个 directWindowDragMutex 串行化，奇数区间本身未包含 Win32/COM 调用。
- WG21 的 seqlock 讨论采用数据读取后的 acquire fence 来阻止末尾序号读取与前面的 payload 读取乱序；末尾的 acquire load 主要约束它后面的操作，不能作为该 fence 的一般替代。[WG21 P1478R8（2022-11-09）](https://www.open-std.org/JTC1/SC22/WG21/docs/papers/2022/p1478r8.html)、[Microsoft 内存序说明](https://learn.microsoft.com/en-us/windows/win32/sync/synchronization-and-multiprocessor-issues)
- 推论：当前读取模式不具备一般 C++ 弱内存模型下的完整 seqlock 证明；在 ARM64 尤其应审查生成指令及写方 fence 配对，混代坐标可以影响拖动命中或布局输入。尚未复现混代快照，不能据此声称本次卡顿来自它，也不建议把全仓 seqlock 改造混入颜色块小修复。
- 可用独立内存模型/ARM64 压力测试验证，但普通压力测试没有读到错误不能证明内存序正确。检查写方 release fence 与读方 acquire fence的完整关系，避免只凭末尾加一个原子 load 的名字判断。

### 5. 大位移目标补查：不能把 mainButton 的任意长动画范围当作真实生产路径

父会话追加核对 capacity 与 predicted viewport envelope 的生产可达条件，本分工只补查目标来源：

- 进入白板的大位移由 `Bar.RenderLoop.cpp:2933`、`:2935` 写入 `state.displayCenterX/Y`，时长 0.4 s；换屏/分辨率切换由 `:1070`、`:1071` 使用同两属性。
- 每帧 `:1177`、`:1178` 调用 `mainButton->x/y.SetDirect(displayCenter.val / currentZoom)`；所以 `:9353`、`:9354` 的 `ValueRange(mainButton x/y)` 看不到 displayCenter 的整个大位移动画。
- 吸附自动居中同样写 displayCenterX（`:7581`）；初次就位 `:2942`、收缩时中心纠正 `:5981`、直拖吸收 `:12955`、`:12957`、缩放 `Bar.Zoom.cppm:70` 都直接设置 mainButton 坐标。
- 因此不支持“普通白板进入留下 mainButton 大跨度 SetTar，predictedEnvelope 包含整条屏幕轨迹”的推断。
- 另一个可达方向是底栏居中展开：`Bar.RenderLoop.cpp:9357` 调用 `ResolveBarBottomDockCenteredRootRange`，将 mainBar 偏移与宽度的独立极值反推 root range，再在 `:9396` 组合回 mainBar range。这种区间组合可能包含实际同批动画不可能同时出现的极值组合，超过按当前位置计算的 capacity。父会话已接管该 source-bounds 模型；这里不宣称真实 ULW 必然失败。

## Files found

- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp`：窗口 Raw Input/timer，鼠标/触摸采样与直接拖动。
- `Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp`：显示快照发布、输入/渲染线程启动顺序、初始光影样式。
- `Inkeys/Inkeys/UI/Bar/Bar.Animation.cppm`：目标写入、曲线、批次时间轴及小粒度事务合同。
- `Inkeys/Inkeys/UI/Bar/Bar.Animation.cpp`：属性推进、异常值回落与动画速度全局。
- `Inkeys/Inkeys/UI/Bar/Bar.Atomic.cppm`：wake signal、sustainFlag 和 renderOnceFlag。
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp`：UpdateRendering 与设置写入口。
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm`：成功呈现/待显示快照、输入共享状态。
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp`：颜色块重复目标的生产调用方、批次时序及大位移来源。
- `Inkeys/Inkeys/UI/Bar/Bar.UI.cpp`：SVG/文字转换在锁外解析并凭 generation 回交；未找到重解析被锁强制串成长等待的证据。
- `Inkeys/Inkeys/UI/Bar/Bar.Zoom.cppm`：主按钮位置由 SetDirect 保持屏幕坐标。
- `Inkeys/IdtAtomic.h`：必须始终 lock-free 的原子封装，不能将其说成全局 mutex 封装。
- `InkeysHeadlessTests/animation_tests.cpp`：已有单属性同目标 no-op、中断、曲线和有限值测试；这些不覆盖主 RenderLoop 的两次不同目标组合。

## Related specs

- `.trellis/workflow.md`：本任务保持调查/规划，批准前不实现。
- `.trellis/spec/native-desktop/index.md`：UI3/Window/渲染线程边界。
- `.trellis/spec/native-desktop/cpp-conventions.md`：最小修改、先找全部共享状态读写方。
- `.trellis/spec/native-desktop/rendering-and-ui.md:57`：idle 唤醒与 dt 合同。
- `.trellis/spec/native-desktop/rendering-and-ui.md:1029`：第三鼠标光 Dormant/Inside/Grace 合同。
- `.trellis/spec/native-desktop/rendering-and-ui.md:1338`：批次中点与剩余时间合同。
- `.trellis/spec/native-desktop/rendering-and-ui.md:1349`：同帧只提交最终目标；本次重复 ft 与该合同直接冲突。

## Caveats / Not Found

- 用户补充的症状包括运行中一会快一会慢、来自其他用户的偶发反馈，不能限定为“进入慢状态后只能重启恢复”。
- 颜色块重复 ft 是已确认源级动画缺陷；目前没有能单独证明其导致全部整体慢帧的实机数据。关闭动态光影也改变输入需求，必须区分绘制开销和需求频度。
- 没有现场 HRESULT、帧耗时、CPU 栈或原始 dt；不能确认 GL/GPU 驱动泄漏、ULW 必然失败、无界鼠标队列或全局动画速度被改写。
- 本报告只对当前读到的代码做判断；前序基线差异由父会话处理。
- 仅执行了上述 research 公式模型；既有 headless 工程和主 Solution 未构建，原因是本阶段为定位且产品实现未获批准。
